#ifndef MOXIE_MCOROUTINE_H
#define MOXIE_MCOROUTINE_H
#include <string.h>
#include <syscall.h>
#include <iostream>
#include <functional>
#include <assert.h>

#include <Mcontext.h>
#include <McoStack.h>
#include <McoCallStack.h>
#include <Log.h>

extern "C"
{
    extern void mcontext_swap(Mcontext *, Mcontext *) asm("mco_swap");
};

namespace moxie {

using Routine = std::function<void ()>;

class McoRoutine {
public:
    McoRoutine(Routine run, bool use_private = false) :
        sink_(nullptr),
		stack_(nullptr),
        callstack_(nullptr),
        ctx_(new Mcontext),
        corun_(run),
		priStack_(use_private),
        main_(false),
        called_(false),
        shouldClose_(false),
        start_(false),
        running_(false),
        done_(false),
        dyield_(false),
        store_(false) {
	}
	~McoRoutine() {
		delete stack_;
		stack_ = nullptr;
		LOGGER_TRACE("McoRoutine will be destroyed.");
	}

    void isMain(bool m) {
        main_ = m;
    }

    bool isMain() {
        return main_;
    }
    
    void resume() {
        if (running_ || (done_ && dyield_)) {
            return;
        }
        assert(callstack_ && callstack_->vaild());
        auto& index = callstack_->index();
        LOGGER_TRACE("callstack index:" << index);
        if (0 >= index) { 
            auto stack = new McoStack(gettid(), 1024 * 1024, true);
            assert(stack && stack->vaild());
            auto mco = new McoRoutine(Empty, true);
            mco->stack(stack);
            mco->callStack(callstack_);
            initMcontext();
            McontextMake(mco->ctx_, (cofunc)McoRoutine::RunRoutine, mco);
            mco->main_ = true;
            mco->start_ = true;
            mco->running_ = true;
            mco->called_ = true;
            (*callstack_)[index] = mco;
            index++;
        }
        assert(index > 0);
        auto cur = (*callstack_)[index - 1];
        if (cur->sink_ != this) {
            if (!start_) {
                start_ = true;
                initMcontext();
                McontextMake(ctx_, (cofunc)McoRoutine::RunRoutine, this);
            }
            running_ = true;
            called_ = true;
            sink_ = cur;
            (*callstack_)[index] = this;
            ++index;
            cur->running_ = false;
            swap(cur, this);
        } else {
            cur->yield();
        }
    }

    void yield() {
        if (!running_ && (done_ && dyield_)) {
            return;
        }
        assert(!main_);
        if (done_) {
            dyield_ = true; // routine is done and do the last yield.
            shouldClose_ = true;
        }
        assert(callstack_ && callstack_->vaild());
        auto& index = callstack_->index();
        assert(index >= 2);
        assert(this == (*callstack_)[index - 1]);
        auto sink = (*callstack_)[index - 2];
        assert(sink_ == sink);
        sink_ = nullptr;
        sink->running_ = true;
        running_ = false;
        called_ = false; // remove from callstack
        (*callstack_)[index - 1] = nullptr;
        LOGGER_TRACE("yield sink=" << (unsigned long)sink);
        LOGGER_TRACE("yield this=" << (unsigned long)this);
        --index;
        swap(this, sink);
    }
    
    static void swap(McoRoutine *sink, McoRoutine *co) {
        char c;
        std::cout << "begin swap" << std::endl;
        sink->stack_->ssp(&c);
        if (!co->priStack_) {
            auto occupy = co->stack_->occupy();
            co->stack_->occupy(co);
            if (occupy && occupy != co
                && !(occupy->done_ && occupy->dyield_)
                && occupy->stack_) {
                occupy->store_ = true;
                LOGGER_TRACE("occupy:" << (unsigned long)occupy);
                McoStack::StoreStack(occupy->stack_);
            }
        }
        std::cout << "before mcontext_swap" << std::endl;
        mcontext_swap(sink->ctx_, co->ctx_);
        std::cout << "after mcontext_swap" << std::endl;
        auto callstack = McoCallStack::CallStack();
        auto cur = callstack->cur();
        std::cout << "cur co=" << (unsigned long)cur << std::endl;
        std::cout << "callstack index=" << (unsigned long)callstack->index() << std::endl;
        std::cout << "callstack size=" << (unsigned long)callstack->size() << std::endl;
        if (cur && cur->store_
            && cur->stack_
            && !cur->priStack_) {
            cur->store_ = false;
            assert(cur->callstack_ == callstack);
            McoStack::RecoverStack(cur->stack_);
        }
        std::cout << "end swap" << std::endl;
    }

    bool callStack(McoCallStack *callstack) {
        if (callstack
            && callstack->vaild()) {
            callstack_ = callstack;
            return true;
        }
        return false;
    }

    bool stack(McoStack *stack) {
        if (stack
            && stack->vaild()) {
            stack_ = stack;
            return true;
        }
        return false;
    }

    static void Empty() {
        assert(false);
    }

    static void RunRoutine(McoRoutine *co) {
        if (co && co->running_) {
            try {
                co->corun_();
            } catch (...) {
                LOGGER_WARN("Some exception happened.");            
            }
            co->done_ = true;
            co->yield();
        }
    }
private:
    void initMcontext() {
        McontextInit(ctx_);
        ctx_->ss_sp = stack_->stack();
        ctx_->ss_size = stack_->size();
    }
private:
    McoRoutine* sink_;
    McoStack* stack_;
    McoCallStack* callstack_;
    Mcontext* ctx_;
    Routine corun_;
    bool priStack_;
    bool main_;
    bool called_;
    bool shouldClose_;
    bool start_;
    bool running_;
    bool done_;
    bool dyield_;
    bool store_;
};

}
#endif