#include "main/lsp/requests/implementation.h"
#include "core/lsp/QueryResponse.h"
#include "main/lsp/json_types.h"
#include "main/lsp/lsp.h"

using namespace std;

namespace sorbet::realmain::lsp {

namespace {
struct MethodImplementationResults {
    vector<core::Loc> locations;
    unique_ptr<ResponseError> error;
};

unique_ptr<ResponseError> makeInvalidParamsError(std::string error) {
    return make_unique<ResponseError>((int)LSPErrorCodes::InvalidParams, error);
}

unique_ptr<ResponseError> makeInvalidRequestError(core::SymbolRef symbol, const core::GlobalState &gs) {
    auto errorMessage = fmt::format("Selected symbol `{}` is not a method or a reference of an abstract class. `Go To "
                                    "Implementation` request can't be applied",
                                    symbol.show(gs));
    return make_unique<ResponseError>((int)LSPErrorCodes::InvalidParams, errorMessage);
}

const MethodImplementationResults findMethodImplementations(const core::GlobalState &gs, core::SymbolRef method) {
    MethodImplementationResults res;
    if (!method.data(gs)->isMethod() || !method.data(gs)->isAbstract()) {
        res.error = makeInvalidRequestError(method, gs);
        return res;
    }

    vector<core::Loc> locations;
    auto owner = method.data(gs)->owner;
    if (!owner.isClassOrModule()) {
        res.error = makeInvalidParamsError("Abstract method can only be inside a class or module");
        return res;
    }

    auto owningClassSymbolRef = owner.asClassOrModuleRef();
    auto includeOwner = false;
    // Scans whole symbol table. This is slow, and we might need to make this faster eventually.
    auto childClasses = getSubclassesSlow(gs, owningClassSymbolRef, includeOwner);
    auto methodName = method.data(gs)->name;
    for (const auto &childClass : childClasses) {
        auto methodImplementation = childClass.data(gs)->findMember(gs, methodName);
        locations.push_back(methodImplementation.data(gs)->loc());
    }

    res.locations = locations;
    return res;
}

core::SymbolRef findOverridedMethod(const core::GlobalState &gs, const core::SymbolRef method) {
    auto ownerClass = method.data(gs)->owner.asClassOrModuleRef();

    for (auto mixin : ownerClass.data(gs)->mixins()) {
        if (!mixin.data(gs)->isClassOrModule() && !mixin.data(gs)->isAbstract()) {
            continue;
        }
        return mixin.data(gs)->findMember(gs, method.data(gs)->name);
    }
    return core::Symbols::noSymbol();
}
} // namespace

ImplementationTask::ImplementationTask(const LSPConfiguration &config, MessageId id,
                                       std::unique_ptr<ImplementationParams> params)
    : LSPRequestTask(config, move(id), LSPMethod::TextDocumentImplementation), params(move(params)) {}

unique_ptr<ResponseMessage> ImplementationTask::runRequest(LSPTypecheckerDelegate &typechecker) {
    auto response = make_unique<ResponseMessage>("2.0", id, LSPMethod::TextDocumentImplementation);
    if (!config.opts.lspGoToImplementationEnabled) {
        response->error = make_unique<ResponseError>(
            (int)LSPErrorCodes::InvalidRequest,
            "The `Go To Implementation` LSP feature is experimental and disabled by default.");
        return response;
    }

    const core::GlobalState &gs = typechecker.state();
    auto queryResult =
        queryByLoc(typechecker, params->textDocument->uri, *params->position, LSPMethod::TextDocumentImplementation);

    if (queryResult.error) {
        // An error happened while setting up the query.
        response->error = move(queryResult.error);
        return response;
    }

    if (queryResult.responses.empty()) {
        return response;
    }

    vector<unique_ptr<Location>> result;
    auto queryResponse = move(queryResult.responses[0]);
    if (auto def = queryResponse->isDefinition()) {
        // User called "Go to Implementation" from the abstract function definition
        core::SymbolRef method = def->symbol;
        if (!method.data(gs)->isMethod()) {
            response->error = makeInvalidRequestError(method, gs);
            return response;
        }

        core::SymbolRef overridedMethod = method;
        if (method.data(gs)->isOverride()) {
            overridedMethod = findOverridedMethod(gs, method);
        }
        auto locationsOrError = findMethodImplementations(gs, overridedMethod);

        if (locationsOrError.error != nullptr) {
            response->error = move(locationsOrError.error);
            return response;
        } else {
            for (const auto &location : locationsOrError.locations) {
                addLocIfExists(gs, result, location);
            }
        }
    } else if (auto constant = queryResponse->isConstant()) {
        // User called "Go to Implementation" from the abstract class reference
        auto classSymbol = constant->symbol;

        if (!classSymbol.data(gs)->isClassOrModule() || !classSymbol.data(gs)->isClassOrModuleAbstract()) {
            response->error = makeInvalidRequestError(classSymbol, gs);
            return response;
        }

        auto includeClassSymbol = false;
        auto classOrModuleRef = classSymbol.asClassOrModuleRef();
        auto childClasses = getSubclassesSlow(gs, classOrModuleRef, includeClassSymbol);
        for (const auto &childClass : childClasses) {
            for (auto loc : childClass.data(gs)->locs()) {
                addLocIfExists(gs, result, loc);
            }
        }

    } else if (auto send = queryResponse->isSend()) {
        auto mainResponse = move(send->dispatchResult->main);

        // User called "Go to Implementation" from the abstract function call
        if (mainResponse.errors.size() != 0) {
            response->error = makeInvalidParamsError("Failed to fetch implementations");
            return response;
        }

        auto calledMethod = mainResponse.method;
        core::SymbolRef overridedMethod = calledMethod;
        if (calledMethod.data(gs)->isOverride()) {
            overridedMethod = findOverridedMethod(gs, overridedMethod);
        }

        auto locationsOrError = findMethodImplementations(gs, overridedMethod);

        if (locationsOrError.error != nullptr) {
            response->error = move(locationsOrError.error);
            return response;
        } else {
            for (const auto &location : locationsOrError.locations) {
                addLocIfExists(gs, result, location);
            }
        }
    }

    response->result = move(result);
    return response;
}
} // namespace sorbet::realmain::lsp
