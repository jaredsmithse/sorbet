#include "core/Types.h"
#include "common/common.h"

using namespace ruby_typer;
using namespace ruby_typer::core;
using namespace std;

// improve debugging.
template class std::shared_ptr<ruby_typer::core::Type>;
template class std::vector<core::Loc>;

shared_ptr<Type> Types::top() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_top());
    return res;
}

shared_ptr<Type> Types::bottom() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_bottom());
    return res;
}

shared_ptr<Type> Types::nil() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_NilClass());
    return res;
}

shared_ptr<Type> Types::dynamic() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_untyped());
    return res;
}
std::shared_ptr<Type> Types::trueClass() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_TrueClass());
    return res;
}

std::shared_ptr<Type> Types::falseClass() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_FalseClass());
    return res;
}

std::shared_ptr<Type> Types::Integer() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_Integer());
    return res;
}

std::shared_ptr<Type> Types::Float() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_Float());
    return res;
}

std::shared_ptr<Type> Types::arrayClass() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_Array());
    return res;
}

std::shared_ptr<Type> Types::hashClass() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_Hash());
    return res;
}

std::shared_ptr<Type> Types::procClass() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_Proc());
    return res;
}

std::shared_ptr<Type> Types::String() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_String());
    return res;
}

std::shared_ptr<Type> Types::Symbol() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_Symbol());
    return res;
}

std::shared_ptr<Type> Types::Object() {
    static auto res = make_shared<ClassType>(core::GlobalState::defn_Object());
    return res;
}

std::shared_ptr<Type> Types::falsyTypes() {
    static auto res = OrType::make_shared(Types::nil(), Types::falseClass());
    return res;
}

std::shared_ptr<Type> Types::dropSubtypesOf(core::Context ctx, std::shared_ptr<Type> from, core::SymbolRef klass) {
    std::shared_ptr<Type> result;

    if (from->isDynamic()) {
        return from;
    }

    typecase(from.get(),
             [&](OrType *o) {
                 if (o->left->derivesFrom(ctx, klass)) {
                     result = Types::dropSubtypesOf(ctx, o->right, klass);
                 } else if (o->right->derivesFrom(ctx, klass)) {
                     result = Types::dropSubtypesOf(ctx, o->left, klass);
                 } else {
                     result = from;
                 }
             },
             [&](AndType *a) {
                 if (a->left->derivesFrom(ctx, klass) || a->right->derivesFrom(ctx, klass)) {
                     result = Types::bottom();
                 } else {
                     result = from;
                 }
             },
             [&](ClassType *c) {
                 if (c->isDynamic()) {
                     result = from;
                 } else if (c->symbol == klass || c->derivesFrom(ctx, klass)) {
                     result = Types::bottom();
                 } else {
                     result = from;
                 }
             },
             [&](Type *) { result = from; });
    ENFORCE(Types::isSubType(ctx, result, from), "dropSubtypesOf(" + from->toString(ctx) + "," +
                                                     klass.info(ctx).fullName(ctx) + ") returned " +
                                                     result->toString(ctx) + ", which is not a subtype of the input");
    return result;
}

bool Types::canBeTruthy(core::Context ctx, std::shared_ptr<Type> what) {
    if (what->isDynamic()) {
        return true;
    }
    auto truthyPart = Types::dropSubtypesOf(ctx, Types::dropSubtypesOf(ctx, what, GlobalState::defn_NilClass()),
                                            GlobalState::defn_FalseClass());
    return !truthyPart->isBottom(); // check if truthyPart is empty
}

bool Types::canBeFalsy(core::Context ctx, std::shared_ptr<Type> what) {
    if (what->isDynamic()) {
        return true;
    }
    return Types::isSubType(ctx, Types::falseClass(), what) ||
           Types::isSubType(ctx, Types::nil(),
                            what); // check if inhabited by falsy values
}

std::shared_ptr<Type> Types::approximateSubtract(core::Context ctx, std::shared_ptr<Type> from,
                                                 std::shared_ptr<Type> what) {
    std::shared_ptr<Type> result;
    typecase(what.get(), [&](ClassType *c) { result = Types::dropSubtypesOf(ctx, from, c->symbol); },
             [&](OrType *o) {
                 result = Types::approximateSubtract(ctx, Types::approximateSubtract(ctx, from, o->left), o->right);
             },
             [&](Type *) { result = from; });
    return result;
}

ruby_typer::core::ClassType::ClassType(ruby_typer::core::SymbolRef symbol) : symbol(symbol) {
    ENFORCE(symbol.exists());
}

namespace {
string classNameToString(GlobalState &gs, core::NameRef nm) {
    core::Name &name = nm.name(gs);
    if (name.kind == core::CONSTANT) {
        return name.cnst.original.toString(gs);
    } else {
        ENFORCE(name.kind == core::UNIQUE);
        ENFORCE(name.unique.uniqueNameKind == core::Singleton);
        return "<class:" + classNameToString(gs, name.unique.original) + ">";
    }
}
}; // namespace

string ruby_typer::core::ClassType::toString(GlobalState &gs, int tabs) {
    return classNameToString(gs, this->symbol.info(gs).name);
}

string ruby_typer::core::ClassType::typeName() {
    return "ClassType";
}

ruby_typer::core::ProxyType::ProxyType(shared_ptr<ruby_typer::core::Type> underlying) : underlying(move(underlying)) {}

void ProxyType::_sanityCheck(core::Context ctx) {
    ENFORCE(cast_type<ClassType>(this->underlying.get()) != nullptr);
    this->underlying->sanityCheck(ctx);
}

bool Type::isDynamic() {
    auto *t = cast_type<ClassType>(this);
    return t != nullptr && t->symbol == core::GlobalState::defn_untyped();
}

bool Type::isBottom() {
    auto *t = cast_type<ClassType>(this);
    return t != nullptr && t->symbol == core::GlobalState::defn_bottom();
}

ruby_typer::core::LiteralType::LiteralType(int64_t val) : ProxyType(Types::Integer()), value(val) {}

ruby_typer::core::LiteralType::LiteralType(double val)
    : ProxyType(Types::Float()), value(*reinterpret_cast<u8 *>(&val)) {}

ruby_typer::core::LiteralType::LiteralType(core::SymbolRef klass, core::NameRef val)
    : ProxyType(klass == core::GlobalState::defn_String() ? Types::String() : Types::Symbol()), value(val._id) {
    ENFORCE(klass == core::GlobalState::defn_String() || klass == core::GlobalState::defn_Symbol());
}

ruby_typer::core::LiteralType::LiteralType(bool val)
    : ProxyType(val ? Types::trueClass() : Types::falseClass()), value(val ? 1 : 0) {}

string LiteralType::typeName() {
    return "LiteralType";
}

string LiteralType::toString(GlobalState &gs, int tabs) {
    string value;
    SymbolRef undSymbol = cast_type<ClassType>(this->underlying.get())->symbol;
    if (undSymbol == GlobalState::defn_String()) {
        value = "\"" + NameRef(gs, this->value).toString(gs) + "\"";
    } else if (undSymbol == GlobalState::defn_Symbol()) {
        value = ":\"" + NameRef(gs, this->value).toString(gs) + "\"";
    } else if (undSymbol == GlobalState::defn_Integer()) {
        value = to_string(this->value);
    } else if (undSymbol == GlobalState::defn_Float()) {
        value = to_string(*reinterpret_cast<double *>(&(this->value)));
    } else if (undSymbol == GlobalState::defn_TrueClass()) {
        value = "true";
    } else if (undSymbol == GlobalState::defn_FalseClass()) {
        value = "false";
    } else {
        Error::raise("should not be reachable");
    }
    return this->underlying->toString(gs, tabs) + "(" + value + ")";
}

ruby_typer::core::TupleType::TupleType(vector<shared_ptr<Type>> &elements)
    : ProxyType(Types::arrayClass()), elems(move(elements)) {}

string TupleType::typeName() {
    return "TupleType";
}

string ShapeType::typeName() {
    return "ShapeType";
}

string MagicType::typeName() {
    return "MagicType";
}

string AliasType::typeName() {
    return "AliasType";
}

string AndType::typeName() {
    return "AndType";
}

AndType::AndType(shared_ptr<Type> left, shared_ptr<Type> right) : left(left), right(right) {}

OrType::OrType(shared_ptr<Type> left, shared_ptr<Type> right) : left(left), right(right) {}

string OrType::typeName() {
    return "OrType";
}

void printTabs(stringstream &to, int count) {
    int i = 0;
    while (i < count) {
        to << "  ";
        i++;
    }
}

string TupleType::toString(GlobalState &gs, int tabs) {
    stringstream buf;
    buf << "TupleType {" << endl;
    int i = 0;
    for (auto &el : this->elems) {
        printTabs(buf, tabs + 1);
        buf << i++ << " = " << el->toString(gs, tabs + 3) << endl;
    }
    printTabs(buf, tabs);
    buf << "}";
    return buf.str();
}

void TupleType::_sanityCheck(core::Context ctx) {
    ProxyType::_sanityCheck(ctx);
}

ruby_typer::core::ShapeType::ShapeType() : ProxyType(Types::hashClass()) {}

ruby_typer::core::ShapeType::ShapeType(vector<shared_ptr<LiteralType>> &keys, vector<shared_ptr<Type>> &values)
    : ProxyType(Types::hashClass()), keys(move(keys)), values(move(values)) {}

string ShapeType::toString(GlobalState &gs, int tabs) {
    stringstream buf;
    buf << "ShapeType {" << endl;
    auto valueIterator = this->values.begin();
    for (auto &el : this->keys) {
        printTabs(buf, tabs + 1);
        buf << el->toString(gs, tabs + 2) << " => " << (*valueIterator)->toString(gs, tabs + 2) << endl;
        ++valueIterator;
    }
    printTabs(buf, tabs);
    buf << "}";
    return buf.str();
}

void ShapeType::_sanityCheck(core::Context ctx) {
    ProxyType::_sanityCheck(ctx);
    ENFORCE(this->values.size() == this->keys.size());
    for (auto &v : this->keys) {
        v->_sanityCheck(ctx);
    }
    for (auto &e : this->values) {
        e->_sanityCheck(ctx);
    }
}

MagicType::MagicType() : ProxyType(make_shared<ClassType>(core::GlobalState::defn_Magic())) {}

string MagicType::toString(GlobalState &gs, int tabs) {
    return underlying->toString(gs, tabs);
}

void MagicType::_sanityCheck(core::Context ctx) {
    ProxyType::_sanityCheck(ctx);
}

AliasType::AliasType(core::SymbolRef other) : symbol(other) {}

string AliasType::toString(GlobalState &gs, int tabs) {
    stringstream buf;
    buf << "AliasType { symbol = " << this->symbol.info(gs).fullName(gs) << " }";
    return buf.str();
}

string AndType::toString(GlobalState &gs, int tabs) {
    stringstream buf;
    bool leftBrace = isa_type<OrType>(this->left.get());
    bool rightBrace = isa_type<OrType>(this->right.get());

    if (leftBrace) {
        buf << "(";
    }
    buf << this->left->toString(gs, tabs + 2);
    if (leftBrace) {
        buf << ")";
    }
    buf << " & ";
    if (rightBrace) {
        buf << "(";
    }
    buf << this->right->toString(gs, tabs + 2);
    if (rightBrace) {
        buf << ")";
    }
    return buf.str();
}

string OrType::toString(GlobalState &gs, int tabs) {
    stringstream buf;
    bool leftBrace = isa_type<AndType>(this->left.get());
    bool rightBrace = isa_type<AndType>(this->right.get());

    if (leftBrace) {
        buf << "(";
    }
    buf << this->left->toString(gs, tabs + 2);
    if (leftBrace) {
        buf << ")";
    }
    buf << " | ";
    if (rightBrace) {
        buf << "(";
    }
    buf << this->right->toString(gs, tabs + 2);
    if (rightBrace) {
        buf << ")";
    }
    return buf.str();
}

void AndType::_sanityCheck(core::Context ctx) {
    left->_sanityCheck(ctx);
    right->_sanityCheck(ctx);
    ENFORCE(!isa_type<ProxyType>(left.get()));
    ENFORCE(!isa_type<ProxyType>(right.get()));
    ENFORCE(!left->isDynamic());
    ENFORCE(!right->isDynamic());
}

void OrType::_sanityCheck(core::Context ctx) {
    left->_sanityCheck(ctx);
    right->_sanityCheck(ctx);
    ENFORCE(!isa_type<ProxyType>(left.get()));
    ENFORCE(!isa_type<ProxyType>(right.get()));
    ENFORCE(!left->isDynamic());
    ENFORCE(!right->isDynamic());
}

int ClassType::kind() {
    return 1;
}

void ClassType::_sanityCheck(core::Context ctx) {
    ENFORCE(this->symbol.exists());
}

int AndType::kind() {
    return 2;
}

int OrType::kind() {
    return 3;
}

bool ClassType::isFullyDefined() {
    return !this->isDynamic();
}

bool LiteralType::isFullyDefined() {
    return true;
}

bool ShapeType::isFullyDefined() {
    return true; // might not be true if we support uninstantiated types inside hashes. For now, we don't
}

bool TupleType::isFullyDefined() {
    return true; // might not be true if we support uninstantiated types inside tuples. For now, we don't
}

bool MagicType::isFullyDefined() {
    return true;
}

bool AliasType::isFullyDefined() {
    return true;
}

bool AndType::isFullyDefined() {
    return this->left->isFullyDefined() && this->right->isFullyDefined();
}

bool OrType::isFullyDefined() {
    return this->left->isFullyDefined() && this->right->isFullyDefined();
}
