#include "priority_ctpl.h"
#include <gtest/gtest.h>
#include <string>

template <typename T>
void verify_pop_order(ctpl::detail::PriorityQueue<T> &pq,
                      const std::initializer_list<T> &order) {
    for(const T &item : order) {
        EXPECT_FALSE(pq.empty());
        T out;
        pq.pop(out);
        EXPECT_EQ(out, item);
    }
    EXPECT_TRUE(pq.empty());
}

TEST(TestPriorityQueue, TestCreate) {
    ctpl::detail::PriorityQueue<int> pq;
    EXPECT_TRUE(pq.empty());
}

TEST(TestPriorityQueue, TestPush) {
    ctpl::detail::PriorityQueue<std::string> pq;
    pq.push(0, 1, "third");
    pq.push(0, 3, "first");
    pq.push(0, 2, "second");

    verify_pop_order<std::string>(pq, {"first", "second", "third"});
}

TEST(TestPriorityQueue, TestPushNoPriority) {
    ctpl::detail::PriorityQueue<int> pq;
    pq.push(7, 0, 5);
    pq.push(6, 0, 2);
    pq.push(1, 0, 3);
    pq.push(2, 0, 6);
    pq.push(4, 0, 1);
    pq.push(3, 0, 4);

    verify_pop_order<int>(pq, {5, 2, 3, 6, 1, 4});
}

TEST(TestPriorityQueue, TestRemoveId) {
    ctpl::detail::PriorityQueue<std::string> pq;
    pq.push(3, 1, "third");
    pq.push(1, 3, "first");
    pq.push(2, 2, "second");

    pq.remove_id(1);

    verify_pop_order<std::string>(pq, {"second", "third"});
}

TEST(TestPriorityQueue, TestRemovePriority) {
    ctpl::detail::PriorityQueue<std::string> pq;
    pq.push(3, 1, "third");
    pq.push(1, 3, "first");
    pq.push(2, 2, "second");

    pq.remove_priority(2);

    verify_pop_order<std::string>(pq, {"first", "third"});
}

TEST(TestPriorityQueue, TestNoRemove) {
    ctpl::detail::PriorityQueue<std::string> pq;
    pq.push(3, 1, "third");
    pq.push(1, 3, "first");
    pq.push(2, 2, "second");

    pq.remove_id(5);
    pq.remove_priority(7);

    verify_pop_order<std::string>(pq, {"first", "second", "third"});
}
