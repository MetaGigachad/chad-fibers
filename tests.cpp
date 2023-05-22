#include <cassert>
#include <iostream>

#include "generators.hpp"

unsigned process(unsigned x);

void generator(unsigned x) {
    if (x == 0) {
        return;
    }
    if (x % 2 == 0) {
        FiberScheduler sched;
        unsigned s = 0;
        sched.schedule([&]() { s = process(x / 2); });
        scheduler_run(sched);
        yield(s);
        return;
    }
    while (true) {
        yield(x);
    }
}

size_t num_of_calls = 0;
size_t num_of_exits = 0;

unsigned process(unsigned x) {
    ++num_of_calls;

    if (x != 0) {
        schedule([=]() { process(x - 1); });
    }
    yield();

    unsigned sum = 0;
    for (auto elem : Generator<unsigned>([=]() { generator(x); })) {
        yield();
        if (x % 2) {
            assert(elem == x);
        }
        sum += elem;
        if (sum > 100) {
            break;
        }
    }

    yield();

    ++num_of_exits;
    return sum;
}

void test_simple() {
    std::cout << __FUNCTION__ << std::endl;

    int x = 0;

    FiberScheduler sched;

    sched.schedule([&]() {
        ++x;
        std::cout << "Done" << std::endl;
    });

    scheduler_run(sched);

    assert(x == 1);
}

void test_multiple() {
    std::cout << __FUNCTION__ << std::endl;

    int x = 0;

    FiberScheduler sched;

    sched.schedule([&]() {
        ++x;
        std::cout << "Done" << std::endl;
    });
    sched.schedule([&]() {
        ++x;
        std::cout << "Done" << std::endl;
    });
    sched.schedule([&]() {
        ++x;
        std::cout << "Done" << std::endl;
    });

    scheduler_run(sched);

    assert(x == 3);
}

void test_recursive() {
    std::cout << __FUNCTION__ << std::endl;

    int x = 0;

    FiberScheduler sched;

    sched.schedule([&]() {
        schedule([&]() {
            ++x;
            std::cout << "Done" << std::endl;
        });
    });
    sched.schedule([&]() {
        schedule([&]() {
            schedule([&]() {
                ++x;
                std::cout << "Done" << std::endl;
            });
        });
    });
    sched.schedule([&]() {
        schedule([&]() {
            schedule([&]() {
                schedule([&]() {
                    ++x;
                    std::cout << "Done" << std::endl;
                });
            });
        });
    });

    scheduler_run(sched);

    assert(x == 3);
}

enum {
    ITERS = 10,
};

void test_yield_one() {
    std::cout << __FUNCTION__ << std::endl;

    int x = 0;

    FiberScheduler sched;

    sched.schedule([&]() {
        for (int i = 0; i != ITERS; ++i) {
            ++x;
            yield();
        }
        std::cout << "Done" << std::endl;
    });

    assert(x == 0);

    scheduler_run(sched);

    assert(x == ITERS);
}

void test_yield_many() {
    std::cout << __FUNCTION__ << std::endl;

    int x = 0;
    int cur_fiber = -1;

    FiberScheduler sched;

    auto create_fiber = [&](int fiber_id) {
        return [&, fiber_id]() {
            for (int i = 0; i != ITERS; ++i) {
                assert(cur_fiber != fiber_id);
                cur_fiber = fiber_id;
                ++x;
                yield();
            }
            std::cout << "Done" << std::endl;
        };
    };

    sched.schedule(create_fiber(1));
    sched.schedule(create_fiber(2));
    sched.schedule(create_fiber(3));

    assert(x == 0);

    scheduler_run(sched);

    assert(x == 3 * ITERS);
}

void test_recursive_sched() {
    std::cout << __FUNCTION__ << std::endl;

    int x = 0;

    FiberScheduler sched;

    sched.schedule([&]() {
        schedule([&] { ++x; });
        yield();

        schedule([&] { ++x; });

        {
            FiberScheduler local_sched;
            scheduler_run(local_sched);
        }

        {
            int back_x = x;

            int y = 0;

            FiberScheduler local_sched;
            local_sched.schedule([&]() {
                ++y;
                yield();
                yield();
                yield();
                yield();
                schedule([&]() { ++y; });
            });

            assert(y == 0);
            scheduler_run(local_sched);
            assert(y == 2);

            assert(back_x == x);
        }

        yield();
        schedule([&]() { ++x; });
    });

    scheduler_run(sched);

    assert(x == 3);
}

void test_recursive_sched_exc() {
    std::cout << __FUNCTION__ << std::endl;

    int x = 0;

    FiberScheduler sched;

    sched.schedule([&]() {
        schedule([&] { ++x; });
        yield();

        schedule([&] { ++x; });

        {
            FiberScheduler local_sched;
            scheduler_run(local_sched);
        }

        {
            int back_x = x;

            int y = 0;

            FiberScheduler local_sched;
            local_sched.schedule([&]() {
                ++y;
                throw TestException();
            });

            assert(y == 0);
            try {
                scheduler_run(local_sched);
                assert(0);
            } catch (const TestException& e) {
                /// SKIP
            }
            assert(y == 1);

            assert(back_x == x);
        }

        yield();
        schedule([&]() { ++x; });
    });

    scheduler_run(sched);

    assert(x == 3);
}

void test_generator() {
    std::cout << __FUNCTION__ << std::endl;

    auto func = []() {
        for (size_t i = 0; i != 10; ++i) {
            size_t elem = i * 2;
            yield(elem);
        }
    };

    for (auto elem : Generator<size_t>(func)) {
        std::cout << elem << std::endl;
    }
    std::cout << "Done" << std::endl;
}

void test_generator_empty() {
    std::cout << __FUNCTION__ << std::endl;

    auto func = []() {};

    for (auto elem : Generator<size_t>(func)) {
        std::cout << elem << std::endl;
    }
    std::cout << "Done" << std::endl;
}

void test_generator_endless() {
    std::cout << __FUNCTION__ << std::endl;

    auto func = []() {
        size_t i = 0;
        while (true) {
            yield(i++);
        }
    };

    size_t sum = 0;
    for (auto elem : Generator<size_t>(func)) {
        std::cout << elem << std::endl;
        sum += elem;
        if (sum > 100) {
            break;
        }
    }
    std::cout << "Done" << std::endl;
}

void test_complex() {
    std::cout << __FUNCTION__ << std::endl;

    int req;
    std::cin >> req;

    FiberScheduler sched;

    sched.schedule([=]() { process(req); });
    sched.schedule([=]() { process(req); });

    scheduler_run(sched);

    std::cout << num_of_calls << ' ' << num_of_exits << std::endl;
}

int main() {
    test_simple();
    test_multiple();
    test_recursive();
    test_yield_one();
    test_yield_many();
    test_recursive_sched();
    test_recursive_sched_exc();
    test_generator();
    test_generator_empty();
    test_generator_endless();
    test_complex();
}
