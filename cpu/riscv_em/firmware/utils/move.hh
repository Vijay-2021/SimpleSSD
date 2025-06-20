#ifndef __RISCV_MOVE__
#define __RISCV_MOVE__

template<typename T>
struct remove_reference {
    using type = T;
};

// Specialization for lvalue reference
template<typename T>
struct remove_reference<T&> {
    using type = T;
};

// Specialization for rvalue reference (C++11+)
template<typename T>
struct remove_reference<T&&> {
    using type = T;
};

template<typename T>
constexpr typename remove_reference<T>::type&& move(T&& t) noexcept {
    return static_cast<typename remove_reference<T>::type&&>(t);
}

#endif