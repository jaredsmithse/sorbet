# frozen_string_literal: true
# typed: true
# compiled: true
# run_filecheck: INITIAL
# run_filecheck: OPT

# Verify that we do reify the block when it's invoked in an ensure block.

def boo(&blk)
  begin
    p "in begin"
  ensure
    p "in ensure"
    yield
  end
end

# INITIAL-LABEL: define i64 @"func_Object#boo"
# INITIAL: call i64 @sorbet_getMethodBlockAsProc
# INITIAL: call i64 @sorbet_run_exception_handling{{.*}}
# INITIAL{LITERAL}: }

# OPT-LABEL: define i64 @"func_Object#boo"
# OPT: call i64 @rb_block_proc
# OPT: call i64 @sorbet_run_exception_handling{{.*}}
# OPT{LITERAL}: }

# INITIAL-LABEL: define internal i64 @"func_Object#boo$block_3"
# INITIAL-NOT: call i64 @sorbet_getMethodBlockAsProc
# INITIAL-NOT: call i64 @rb_block_proc
# INITIAL: @sorbet_i_send(%struct.FunctionInlineCache* @ic_call
# INITIAL-NOT: call i64 @sorbet_getMethodBlockAsProc
# INITIAL-NOT: call i64 @rb_block_proc
# INITIAL{LITERAL}: }

boo do
  puts "boohey"
end

