#include <random>
#include <set>
#include <iostream>
#include "zipfian_int_distribution.h"

int main() {
    std::default_random_engine generator;
    zipfian_int_distribution<int> distribution(1, 500, 0.9);
    std::set<int> s;
    for (int i = 0; i < 500; i++) {
        s.emplace(distribution(generator));
    }
    std::cout << s.size() << std::endl;
}