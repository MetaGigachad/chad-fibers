#pragma once

#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>

using Fiber = std::function<void()>;

class StackPool {
public:
    enum {
        STACK_SIZE = 1024 * 1024 * 4,
    };

    ~StackPool() {
        for (auto elem : stacks) {
            std::free(elem);
        }
    }

    struct Stack {
        StackPool* sp = nullptr;
        void* ptr = nullptr;

        Stack() = default;

        Stack(StackPool* sp, void* ptr) : sp(sp), ptr(ptr) {}

        ~Stack() { free(); }

        Stack(Stack&& other) : sp(other.sp), ptr(other.ptr) { other.ptr = nullptr; }

        Stack& operator=(Stack&& other) noexcept {
            free();
            sp = other.sp;
            std::swap(ptr, other.ptr);
            return *this;
        }

        Stack(const Stack& other) = delete;
        void operator=(const Stack& other) = delete;

        void free() noexcept {
            if (!ptr) {
                return;
            }
            sp->free(ptr);
            ptr = nullptr;
        }
    };

    Stack alloc() {
        if (!stacks.empty()) {
            auto ptr = stacks.back();
            stacks.pop_back();
            return {this, ptr};
        }
        auto ptr = std::calloc(1, STACK_SIZE);
        return {this, ptr};
    }

    void free(void* stack) { stacks.push_back(stack); }

private:
    std::vector<void*> stacks;
} stack_pool;

class FiberScheduler;

struct Action {
    enum {
        START,
        STOP,
        SCHED,
    } action;

    void* user_data = nullptr;
};

class Watch;

struct Context {
    std::unique_ptr<Fiber> fiber;
    StackPool::Stack stack;

    intptr_t eip = 0;
    intptr_t esp = 0;
    std::shared_ptr<Watch> watch;
    std::exception_ptr exception {};

    Context() = default;

    explicit Context(Fiber fiber)
        : fiber(std::make_unique<Fiber>(std::move(fiber))),
          stack(stack_pool.alloc()),
          esp(reinterpret_cast<intptr_t>(stack.ptr) + StackPool::STACK_SIZE) {}

    Context(Context&& other) = default;

    Context(const Context& other) = delete;

    Context& operator=(Context&& other) = default;

    void operator=(const Context& other) = delete;

    /// swap current eip and esp
    Action switch_context(Action);  // TODO
};

class Watch {
public:
    virtual ~Watch() = default;

    /// Inspect context after execution
    virtual void operator()(Action&, Context&) = 0;
};

class FiberScheduler {
public:
    friend class Watch;
    /// Start scheduler event loop
    friend void scheduler_run(FiberScheduler& sched);  // TODO
    /// Fiber simple trampoline
    friend void trampoline(Fiber* fiber);  // TODO

    ~FiberScheduler() { assert(queue.empty()); }

    void schedule(Fiber fiber) { schedule(create_context_from_fiber(std::move(fiber))); }

    void schedule(Context context) { queue.push(std::move(context)); }

    /// Prepare stack, execution, arguments, etc...
    static Context create_context_from_fiber(Fiber fiber);  // TODO

    /// Reschedule self to end of queue
    void* yield(void*);  // TODO

    template <class Watch, class... Args>
    void create_current_fiber_watch(Args... args) {
        sched_context.watch = std::make_shared<Watch>(args...);
    }

private:
    /// Proceed till queue is not empty
    void run();  // TODO

    std::queue<Context> queue;
    Context sched_context;
};

FiberScheduler* current_scheduler = nullptr;

inline void schedule(Fiber fiber) {
    if (!current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    current_scheduler->schedule(std::move(fiber));
}

inline void yield() {
    if (!current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    current_scheduler->yield(nullptr);
}

template <class Data>
void yield(Data data) {
    if (!current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    current_scheduler->yield(&data);
}

template <class Watch, class... Args>
void create_current_fiber_watch(Args... args) {
    if (!current_scheduler) {
        throw std::runtime_error("Global scheduler is empty");
    }
    current_scheduler->create_current_fiber_watch<Watch>(args...);
}

class TestException : public std::exception {};

template <class Result>
class Generator {
    class Iterator {
        /// Inspect generator function context
        class GeneratorWatch : public Watch {
        public:
            explicit GeneratorWatch(Iterator* it) : it(it) {}

            void operator()(Action& action, Context& context) override {
                it->next_ = reinterpret_cast<Result*>(action.user_data);
                if (action.action == Action::STOP) {
                    it->stop_ = true;
                }
                if (action.action == Action::SCHED) {
                    action.action = Action::STOP;
                    it->context_ = std::make_optional<Context>(std::move(context));
                }
            }

            Iterator* it;
        };

        void update() {
            next_ = {};
            if (stop_) {
                return;
            }

            if (!context_) {
                stop_ = true;
                return;
            }
            FiberScheduler sheduler;
            sheduler.schedule(std::move(*context_));
            scheduler_run(sheduler);
        }

    public:
        Iterator() = default;

        explicit Iterator(Fiber fiber) {
            context_ = Context(FiberScheduler::create_context_from_fiber(
                [this, fiber = std::move(fiber)]() {
                    /// self inspect
                    create_current_fiber_watch<GeneratorWatch>(this);
                    fiber();
                }));
            update();
        }

        auto operator*() {
            if (!next_) {
                throw std::runtime_error("Empty");
            }
            auto val = std::move(*next_);
            update();
            return val;
        }

        auto operator->() {
            if (!next_) {
                throw std::runtime_error("Empty");
            }
            auto val = std::move(*next_);
            update();
            return val;
        }

        Iterator& operator++() { return *this; }

        Iterator operator++(int) = delete;

        friend bool operator!=(const Iterator& a, const Iterator& b) {
            return a.next_ || b.next_;
        };

    private:
        std::optional<Context> context_;
        Result* next_ = nullptr;
        bool stop_ = false;
    };

public:
    explicit Generator(Fiber func) : func(std::move(func)) {}

    Iterator begin() {
        if (!func) {
            throw std::runtime_error("Duplicated run");
        }
        Fiber f = std::move(func.value());
        func = {};
        return Iterator(std::move(f));
    }

    Iterator end() { return Iterator(); }

    std::optional<Fiber> func;
};

inline Action Context::switch_context(Action action) {
    Action* action_ptr = &action;
    asm volatile(
        "pusha\n\t"
        "push %2\n\t"
        "movl (%1), %%esi\n\t"
        "movl %%esp, (%1)\n\t"
        "movl %%esi, %%esp\n\t"
        "movl (%0), %%edi\n\t"
        "movl $continue%=, (%0)\n\t"
        "jmp *%%edi\n\t"
        "continue%=:\n\t"
        "pop %%eax\n\t"
        "movl %3, (%%eax)\n\t"
        "popa"
        :
        : "a"(&this->eip), "b"(&this->esp), "c"(&action_ptr), "d"(action_ptr)
        : "cc", "memory", "esi", "edi");
    return *action_ptr;
}

inline void trampoline(Fiber* fiber) {
    try {
        (*fiber)();
    } catch (...) {
        current_scheduler->sched_context.exception = std::current_exception();
    }

    current_scheduler->sched_context.switch_context({.action = Action::STOP});
    __builtin_unreachable();
}

inline void scheduler_run(FiberScheduler& sched) {
    auto* prev_sched = current_scheduler;
    current_scheduler = &sched;
    try {
        current_scheduler->run();
    } catch (...) {
        current_scheduler = prev_sched;
        std::rethrow_exception(std::current_exception());
    }
    current_scheduler = prev_sched;
}

inline Context FiberScheduler::create_context_from_fiber(Fiber fiber) {
    auto context = Context(std::move(fiber));

    intptr_t esp = reinterpret_cast<intptr_t>(context.stack.ptr) + StackPool::STACK_SIZE;
    intptr_t fiber_addr = reinterpret_cast<intptr_t>(context.fiber.get());
    esp -= sizeof(fiber_addr);
    memcpy(reinterpret_cast<void*>(esp), &fiber_addr, sizeof(fiber_addr));
    esp -= sizeof(fiber_addr);
    memset(reinterpret_cast<void*>(esp), 0, sizeof(fiber_addr));

    context.eip = reinterpret_cast<intptr_t>(trampoline);
    context.esp = esp;

    return context;
}

inline void* FiberScheduler::yield(void* data) {
    current_scheduler->sched_context.switch_context(
        {.action = Action::SCHED, .user_data = data});
    return data;
}

inline void FiberScheduler::run() {
    while (!queue.empty()) {
        sched_context = std::move(queue.front());
        queue.pop();
        sched_context.exception = nullptr;
        auto action = sched_context.switch_context({.action = Action::START});

        if (sched_context.exception) {
            std::rethrow_exception(sched_context.exception);
        }

        if (sched_context.watch) {
            (*sched_context.watch)(action, sched_context);
        }

        if (action.action == Action::SCHED) {
            queue.push(std::move(current_scheduler->sched_context));
        }
    }
}
