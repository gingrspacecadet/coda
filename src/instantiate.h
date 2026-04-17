#ifndef INSTANTIATE_H
#define INSTANTIATE_H

#define GET_MACRO(_1, _2, _3, NAME, ...) NAME

#define INSTANTIATE(...) \
    GET_MACRO(__VA_ARGS__, INSTANTIATE_3, INSTANTIATE_2)(__VA_ARGS__)

#define INSTANTIATE_2(T, M) M(T, T)
#define INSTANTIATE_3(T, N, M) M(T, N)

#endif