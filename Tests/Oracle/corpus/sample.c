#include <stdio.h>

struct Point {
    int x;
    int y;
};

int total(const int* values, int count)
{
    int sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += values[i];
    }
    return sum;
}
