//
// Created by Keren Dong on 2019-05-25.
//

#ifndef KUNGFU_COMMON_H
#define KUNGFU_COMMON_H

#include <cstdlib>
#include <cstdint>
#include <string>
#include <sstream>
#include <boost/hana.hpp>
#include <nlohmann/json.hpp>

#ifdef BOOST_HANA_CONFIG_ENABLE_STRING_UDL
using namespace boost::hana::literals;
#define HANA_STR(STR) STR##_s
#else
#define HANA_STR(STR) BOOST_HANA_STRING(STR)
#endif

#define DECLARE_PTR(X) typedef std::shared_ptr<X> X##_ptr;   /** define smart ptr */
#define FORWARD_DECLARE_PTR(X) class X; DECLARE_PTR(X)      /** forward defile smart ptr */

#ifndef _WIN32
#define KF_PACK_TYPE_BEGIN
#define KF_PACK_TYPE_END __attribute__((packed));
#else
#define KF_PACK_TYPE_BEGIN __pragma(pack(push, 1))
#define KF_PACK_TYPE_END ;__pragma(pack(pop))
#endif

#define KF_DEFINE_DATA_TYPE(NAME, TAG, PRIMARY_KEYS, ...) \
struct NAME : public kungfu::data<NAME> { \
    static constexpr int32_t tag = TAG; \
    static constexpr auto primary_keys = PRIMARY_KEYS; \
    NAME() {}; \
    explicit NAME(const char *address) : kungfu::data<NAME>(address) {}; \
    BOOST_HANA_DEFINE_STRUCT(NAME, __VA_ARGS__); \
}

#define KF_DEFINE_PACK_TYPE(NAME, TAG, PRIMARY_KEYS, ...) KF_PACK_TYPE_BEGIN KF_DEFINE_DATA_TYPE(NAME, TAG, PRIMARY_KEYS, __VA_ARGS__) KF_PACK_TYPE_END

#define KF_DEFINE_MARK_TYPE(NAME, TAG) \
struct NAME : public kungfu::data<NAME> { \
    static constexpr int32_t tag = TAG; \
    static constexpr auto primary_keys = boost::hana::make_tuple(); \
}

#define PK(...) boost::hana::make_tuple(MAKE_PK(BOOST_HANA_PP_NARG(__VA_ARGS__), __VA_ARGS__))

#ifdef BOOST_HANA_WORKAROUND_MSVC_PREPROCESSOR_616033
// refer to boost hana BOOST_HANA_DEFINE_STRUCT
#define MAKE_PK(N, ...) BOOST_HANA_PP_CONCAT(BOOST_HANA_PP_CONCAT(MAKE_PK_IMPL_, N)(__VA_ARGS__),)
#else
#define MAKE_PK(N, ...) BOOST_HANA_PP_CONCAT(MAKE_PK_IMPL_, N)(__VA_ARGS__)
#endif

#define MAKE_PK_IMPL_1(k) HANA_STR(#k)
#define MAKE_PK_IMPL_2(k1, k2) HANA_STR(#k1), HANA_STR(#k2)
#define MAKE_PK_IMPL_3(k1, k2, k3) HANA_STR(#k1), HANA_STR(#k2), HANA_STR(#k3)
#define MAKE_PK_IMPL_4(k1, k2, k3, k4) HANA_STR(#k1), HANA_STR(#k2), HANA_STR(#k3), HANA_STR(#k4)

namespace kungfu
{
    uint32_t hash_32(const unsigned char *key, int32_t length);

    template<typename V, size_t N, typename = void>
    struct array_to_string;

    template<typename V, size_t N>
    struct array_to_string<V, N, std::enable_if_t<std::is_same_v<V, char>>>
    {
        std::string operator()(const V *v)
        {
            return std::string(v);
        };
    };

    template<typename V, size_t N>
    struct array_to_string<V, N, std::enable_if_t<not std::is_same_v<V, char>>>
    {
        std::string operator()(const V *v)
        {
            std::stringstream ss;
            ss << "[";
            for (int i = 0; i < N; i++)
            {
                ss << (i > 0 ? "," : "") << v[i];
            }
            ss << "]";
            return ss.str();
        };
    };

    KF_PACK_TYPE_BEGIN
    template<typename T, size_t N>
    struct array
    {
        using type = T[N];
        type value;

        array()
        {
            memset(value, 0, sizeof(type));
        }

        explicit array(const T *t)
        {
            memcpy(value, t, sizeof(value));
        }

        explicit array(const unsigned char *t)
        {
            memcpy(value, t, sizeof(value));
        }

        operator T *()
        {
            return static_cast<T *>(value);
        }

        operator const T *() const
        {
            return static_cast<const T *>(value);
        }

        operator std::string() const
        {
            return array_to_string<T, N>{}(value);
        }

        T &operator[](int i) const
        {
            return const_cast<T &>(value[i]);
        }

        array<T, N> &operator=(const array<T, N> &v)
        {
            memcpy(value, v.value, sizeof(value));
            return *this;
        }
    }
    KF_PACK_TYPE_END

    template<typename T, size_t N>
    void to_json(nlohmann::json &j, const array<T, N> &value)
    {
        j = value.value;
    }

    template<typename T, size_t N>
    void from_json(const nlohmann::json &j, array<T, N> &value)
    {
        for (int i = 0; i < N; i++)
        {
            value.value[i] = j[i].get<T>();
        }
    }

    template<typename T>
    struct is_array
    {
        static constexpr bool value = false;
    };

    template<typename T, size_t N>
    struct is_array<array<T, N>>
    {
        static constexpr bool value = true;
    };

    template<typename T>
    static constexpr bool is_array_v = is_array<T>::value;

    template<typename T, typename = void>
    struct size_fixed
    {
        static constexpr bool value = false;
    };

    template<typename DataType>
    struct size_fixed<DataType, std::enable_if_t<std::is_fundamental_v<DataType> or std::is_enum_v<DataType>>>
    {
        static constexpr bool value = true;
    };

    template<typename DataType>
    struct size_fixed<DataType, std::enable_if_t<std::is_class_v<DataType> and DataType::tag>>
    {
        static constexpr bool value = boost::hana::fold(boost::hana::transform(boost::hana::accessors<DataType>(), [](auto it)
        {
            auto accessor = boost::hana::second(it);
            using AttrType = std::decay_t<decltype(accessor(std::forward<DataType &>(DataType{})))>;
            return std::is_arithmetic_v<AttrType> or std::is_enum_v<AttrType> or is_array_v<AttrType>;
        }), std::logical_and());
    };

    template<typename DataType>
    static constexpr bool size_fixed_v = size_fixed<DataType>::value;

    template<typename, typename = void>
    struct hash;

    template<typename T>
    struct hash<T, std::enable_if_t<std::is_integral_v<T> and not std::is_pointer_v<T>>>
    {
        uint64_t operator()(const T &value)
        {
            return value;
        }
    };

    template<typename T>
    struct hash<T, std::enable_if_t<std::is_pointer_v<T>>>
    {
        uint64_t operator()(const T &value)
        {
            return hash_32(reinterpret_cast<const unsigned char *>(value), sizeof(value));
        }
    };

    template<>
    struct hash<std::string>
    {
        uint64_t operator()(const std::string &value)
        {
            return hash_32(reinterpret_cast<const unsigned char *>(value.c_str()), value.length());
        }
    };

    template<typename T, size_t N>
    struct hash<array<T, N>>
    {
        uint64_t operator()(const array<T, N> &value)
        {
            return hash_32(reinterpret_cast<const unsigned char *>(value.value), sizeof(value));
        }
    };

    template<typename DataType>
    struct data
    {
        static constexpr bool reflect = true;

        data()
        {
            boost::hana::for_each(boost::hana::accessors<DataType>(), [&, this](auto it)
            {
                auto accessor = boost::hana::second(it);
                auto &v = accessor(*const_cast<DataType *>(reinterpret_cast<const DataType *>(this)));
                init_member(v);
            });
        }

        explicit data(const char *address)
        {
            nlohmann::json jobj = nlohmann::json::parse(address);
            boost::hana::for_each(boost::hana::accessors<DataType>(), [&, this](auto it)
            {
                auto name = boost::hana::first(it);
                auto accessor = boost::hana::second(it);
                auto &j = jobj[name.c_str()];
                auto &v = accessor(*const_cast<DataType *>(reinterpret_cast<const DataType *>(this)));
                restore_from_json(j, v);
            });
        }

        [[nodiscard]] std::string to_string() const
        {
            nlohmann::json j{};
            boost::hana::for_each(boost::hana::accessors<DataType>(), [&, this](auto it)
            {
                auto name = boost::hana::first(it);
                auto accessor = boost::hana::second(it);
                using AttrType = std::decay_t<decltype(accessor(std::forward<DataType &>(DataType{})))>;
                j[name.c_str()] = accessor(*reinterpret_cast<const DataType *>(this));
            });
            return j.dump();
        }

        explicit operator std::string() const
        {
            return to_string();
        }

        [[nodiscard]] uint64_t uid() const
        {
            auto primary_keys = boost::hana::transform(DataType::primary_keys, [this](auto pk)
            {
                auto just = boost::hana::find_if(boost::hana::accessors<DataType>(), [&](auto it)
                {
                    return pk == boost::hana::first(it);
                });
                auto accessor = boost::hana::second(*just);
                auto value = accessor(*(reinterpret_cast<const DataType *>(this)));
                return hash<decltype(value)>{}(value);
            });
            return boost::hana::fold(primary_keys, std::bit_xor());
        }

    private:
        template<typename V>
        static std::enable_if_t<std::is_arithmetic_v<std::decay_t<V>>, void> init_member(V &v)
        {
            v = 0;
        }

        template<typename V>
        static std::enable_if_t<not std::is_arithmetic_v<std::decay_t<V>>, void> init_member(V &v)
        {}

        template<typename J, typename V>
        static std::enable_if_t<std::is_arithmetic_v<std::decay_t<V>>, void> restore_from_json(J &j, V &v)
        {
            v = j;
        }

        template<typename J, typename V>
        static std::enable_if_t<not std::is_arithmetic_v<std::decay_t<V>>, void> restore_from_json(J &j, V &v)
        {
            j.get_to(v);
        }
    };

    template<typename>
    struct member_pointer_trait;

    template<template<typename MemberPtr, MemberPtr ptr> typename T, typename MemberPtr, MemberPtr ptr>
    struct member_pointer_trait<T<MemberPtr, ptr>>
    {
        static constexpr MemberPtr pointer()
        {
            return ptr;
        }
    };

    struct event
    {
        virtual ~event() = default;

        [[nodiscard]] virtual int64_t gen_time() const = 0;

        [[nodiscard]] virtual int64_t trigger_time() const = 0;

        [[nodiscard]] virtual int32_t msg_type() const = 0;

        [[nodiscard]] virtual uint32_t source() const = 0;

        [[nodiscard]] virtual uint32_t dest() const = 0;

        [[nodiscard]] virtual uint32_t data_length() const = 0;

        [[nodiscard]] virtual const void *data_address() const = 0;

        [[nodiscard]] virtual const char *data_as_bytes() const = 0;

        [[nodiscard]] virtual std::string data_as_string() const = 0;

        [[nodiscard]] virtual std::string to_string() const = 0;

        /**
         * Using auto with the return mess up the reference with the undlerying memory address, DO NOT USE it.
         * @tparam T
         * @return a casted reference to the underlying memory address
         */
        template<typename T>
        std::enable_if_t<size_fixed_v<T>, const T &> data() const
        {
            return *(reinterpret_cast<const T *>(data_address()));
        }

        template<typename T>
        std::enable_if_t<not size_fixed_v<T>, const T> data() const
        {
            return T(data_as_bytes());
        }
    };

    DECLARE_PTR(event)
}

#endif //KUNGFU_COMMON_H
