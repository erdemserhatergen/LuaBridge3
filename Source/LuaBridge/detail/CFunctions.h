// https://github.com/kunitoki/LuaBridge3
// Copyright 2020, Lucio Asnaghi
// Copyright 2019, Dmitry Tarakanov
// Copyright 2012, Vinnie Falco <vinnie.falco@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "Config.h"
#include "FuncTraits.h"
#include "LuaHelpers.h"

#include <string>

namespace luabridge {
namespace detail {

struct CFunc
{
    static void addGetter(lua_State* L, const char* name, int tableIndex)
    {
        assert(name != nullptr);
        assert(lua_istable(L, tableIndex));
        assert(lua_iscfunction(L, -1)); // Stack: getter

        lua_rawgetp(L, tableIndex, getPropgetKey()); // Stack: getter, propget table (pg)
        lua_pushvalue(L, -2); // Stack: getter, pg, getter
        rawsetfield(L, -2, name); // Stack: getter, pg
        lua_pop(L, 2); // Stack: -
    }

    static void addSetter(lua_State* L, const char* name, int tableIndex)
    {
        assert(name != nullptr);
        assert(lua_istable(L, tableIndex));
        assert(lua_iscfunction(L, -1)); // Stack: setter

        lua_rawgetp(L, tableIndex, getPropsetKey()); // Stack: setter, propset table (ps)
        lua_pushvalue(L, -2); // Stack: setter, ps, setter
        rawsetfield(L, -2, name); // Stack: setter, ps
        lua_pop(L, 2); // Stack: -
    }

    //----------------------------------------------------------------------------
    /**
        __index metamethod for a namespace or class static and non-static members.
        Retrieves functions from metatables and properties from propget tables.
        Looks through the class hierarchy if inheritance is present.
    */
    static int indexMetaMethod(lua_State* L)
    {
        assert(lua_istable(L, 1) || lua_isuserdata(L, 1)); // Stack (further not shown): table | userdata, name

        lua_getmetatable(L, 1); // Stack: class/const table (mt)
        assert(lua_istable(L, -1));

        for (;;)
        {
            lua_pushvalue(L, 2); // Stack: mt, field name
            lua_rawget(L, -2); // Stack: mt, field | nil

            if (lua_iscfunction(L, -1)) // Stack: mt, field
            {
                lua_remove(L, -2); // Stack: field
                return 1;
            }

            assert(lua_isnil(L, -1)); // Stack: mt, nil
            lua_pop(L, 1); // Stack: mt

            lua_rawgetp(L, -1, getPropgetKey()); // Stack: mt, propget table (pg)
            assert(lua_istable(L, -1));

            lua_pushvalue(L, 2); // Stack: mt, pg, field name
            lua_rawget(L, -2); // Stack: mt, pg, getter | nil
            lua_remove(L, -2); // Stack: mt, getter | nil

            if (lua_iscfunction(L, -1)) // Stack: mt, getter
            {
                lua_remove(L, -2); // Stack: getter
                lua_pushvalue(L, 1); // Stack: getter, table | userdata
                lua_call(L, 1, 1); // Stack: value
                return 1;
            }

            assert(lua_isnil(L, -1)); // Stack: mt, nil
            lua_pop(L, 1); // Stack: mt

            // It may mean that the field may be in const table and it's constness violation.
            // Don't check that, just return nil

            // Repeat the lookup in the parent metafield,
            // or return nil if the field doesn't exist.
            lua_rawgetp(L, -1, getParentKey()); // Stack: mt, parent mt | nil

            if (lua_isnil(L, -1)) // Stack: mt, nil
            {
                lua_remove(L, -2); // Stack: nil
                return 1;
            }

            // Removethe  metatable and repeat the search in the parent one.
            assert(lua_istable(L, -1)); // Stack: mt, parent mt
            lua_remove(L, -2); // Stack: parent mt
        }

        // no return
    }

    //----------------------------------------------------------------------------
    /**
        __newindex metamethod for namespace or class static members.
        Retrieves properties from propset tables.
    */
    static int newindexStaticMetaMethod(lua_State* L)
    {
        return newindexMetaMethod(L, false);
    }

    //----------------------------------------------------------------------------
    /**
        __newindex metamethod for non-static members.
        Retrieves properties from propset tables.
    */
    static int newindexObjectMetaMethod(lua_State* L)
    {
        return newindexMetaMethod(L, true);
    }

    static int newindexMetaMethod(lua_State* L, bool pushSelf)
    {
        assert(lua_istable(L, 1) || lua_isuserdata(L, 1)); // Stack (further not shown): table | userdata, name, new value

        lua_getmetatable(L, 1); // Stack: metatable (mt)
        assert(lua_istable(L, -1));

        for (;;)
        {
            lua_rawgetp(L, -1, getPropsetKey()); // Stack: mt, propset table (ps) | nil

            if (lua_isnil(L, -1)) // Stack: mt, nil
            {
                lua_pop(L, 2); // Stack: -
                return luaL_error(L, "No member named '%s'", lua_tostring(L, 2));
            }

            assert(lua_istable(L, -1));

            lua_pushvalue(L, 2); // Stack: mt, ps, field name
            lua_rawget(L, -2); // Stack: mt, ps, setter | nil
            lua_remove(L, -2); // Stack: mt, setter | nil

            if (lua_iscfunction(L, -1)) // Stack: mt, setter
            {
                lua_remove(L, -2); // Stack: setter
                if (pushSelf)
                    lua_pushvalue(L, 1); // Stack: setter, table | userdata
                lua_pushvalue(L, 3); // Stack: setter, table | userdata, new value
                lua_call(L, pushSelf ? 2 : 1, 0); // Stack: -
                return 0;
            }

            assert(lua_isnil(L, -1)); // Stack: mt, nil
            lua_pop(L, 1); // Stack: mt

            lua_rawgetp(L, -1, getParentKey()); // Stack: mt, parent mt | nil

            if (lua_isnil(L, -1)) // Stack: mt, nil
            {
                lua_pop(L, 1); // Stack: -
                return luaL_error(L, "No writable member '%s'", lua_tostring(L, 2));
            }

            assert(lua_istable(L, -1)); // Stack: mt, parent mt
            lua_remove(L, -2); // Stack: parent mt
            // Repeat the search in the parent
        }

        // no return
    }

    //----------------------------------------------------------------------------
    /**
        lua_CFunction to report an error writing to a read-only value.

        The name of the variable is in the first upvalue.
    */
    static int readOnlyError(lua_State* L)
    {
        std::string s;

        s = s + "'" + lua_tostring(L, lua_upvalueindex(1)) + "' is read-only";

        return luaL_error(L, s.c_str());
    }

    //----------------------------------------------------------------------------
    /**
        lua_CFunction to get a variable.

        This is used for global variables or class static data members.

        The pointer to the data is in the first upvalue.
    */
    template <class T>
    static int getVariable(lua_State* L)
    {
        assert(lua_islightuserdata(L, lua_upvalueindex(1)));
        T const* ptr = static_cast<T const*>(lua_touserdata(L, lua_upvalueindex(1)));
        assert(ptr != nullptr);
        Stack<T>::push(L, *ptr);
        return 1;
    }

    //----------------------------------------------------------------------------
    /**
        lua_CFunction to set a variable.

        This is used for global variables or class static data members.

        The pointer to the data is in the first upvalue.
    */
    template <class T>
    static int setVariable(lua_State* L)
    {
        assert(lua_islightuserdata(L, lua_upvalueindex(1)));
        T* ptr = static_cast<T*>(lua_touserdata(L, lua_upvalueindex(1)));
        assert(ptr != nullptr);
        *ptr = Stack<T>::get(L, 1);
        return 0;
    }

    //--------------------------------------------------------------------------
    /**
        __gc metamethod for a class.
    */
    template <class C>
    static int gcMetaMethod(lua_State* L)
    {
        Userdata* const ud = Userdata::getExact<C>(L, 1);
        ud->~Userdata();
        return 0;
    }

    /**
        __gc metamethod for an arbitrary class.
    */
    template <class T>
    static int gcMetaMethodAny(lua_State* L)
    {
        assert(isfulluserdata(L, 1));
        T* t = align<T>(lua_touserdata(L, 1));
        t->~T();
        return 0;
    }

    //--------------------------------------------------------------------------
    /**
        lua_CFunction to get a class data member.

        The pointer-to-member is in the first upvalue.
        The class userdata object is at the top of the Lua stack.
    */
    template <class C, typename T>
    static int getProperty(lua_State* L)
    {
        C* const c = Userdata::get<C>(L, 1, true);
        T C::** mp = static_cast<T C::**>(lua_touserdata(L, lua_upvalueindex(1)));
        try
        {
            Stack<T&>::push(L, c->**mp);
        }
        catch (const std::exception& e)
        {
            luaL_error(L, e.what());
        }
        return 1;
    }

    //--------------------------------------------------------------------------
    /**
        lua_CFunction to set a class data member.

        The pointer-to-member is in the first upvalue.
        The class userdata object is at the top of the Lua stack.
    */
    template <class C, typename T>
    static int setProperty(lua_State* L)
    {
        C* const c = Userdata::get<C>(L, 1, false);
        T C::** mp = static_cast<T C::**>(lua_touserdata(L, lua_upvalueindex(1)));
        try
        {
            c->** mp = Stack<T>::get(L, 2);
        }
        catch (const std::exception& e)
        {
            luaL_error(L, e.what());
        }
        return 0;
    }
};

//----------------------------------------------------------------------------
/**
    lua_CFunction to call a class member function with a return value.

    The member function pointer is in the first upvalue.
    The class userdata object is at the top of the Lua stack.
*/
template <class F, class T>
int invoke_member_function(lua_State* L)
{
    using FnTraits = detail::function_traits<F>;

    assert(isfulluserdata(L, lua_upvalueindex(1)));

    T* const ptr = Userdata::get<T>(L, 1, false);

    F const& func = *static_cast<const F*>(lua_touserdata(L, lua_upvalueindex(1)));
    assert(func != nullptr);

    return dispatcher<2, typename FnTraits::result_type, typename FnTraits::argument_types>::call(L, ptr, func);
}

template <class F, class T>
int invoke_const_member_function(lua_State* L)
{
    using FnTraits = detail::function_traits<F>;

    assert(isfulluserdata(L, lua_upvalueindex(1)));

    const T* const ptr = Userdata::get<T>(L, 1, true);

    F const& func = *static_cast<const F*>(lua_touserdata(L, lua_upvalueindex(1)));
    assert(func != nullptr);

    return dispatcher<2, typename FnTraits::result_type, typename FnTraits::argument_types>::call(L, ptr, func);
}

//--------------------------------------------------------------------------
/**
    lua_CFunction to call a class member lua_CFunction.

    The member function pointer is in the first upvalue.
    The object userdata ('this') value is at top ot the Lua stack.
*/
template <class T>
int invoke_member_cfunction(lua_State* L)
{
    using F = int (T::*)(lua_State * L);

    assert(isfulluserdata(L, lua_upvalueindex(1)));

    T* const t = Userdata::get<T>(L, 1, false);
    
    F const& func = *static_cast<const F*>(lua_touserdata(L, lua_upvalueindex(1)));
    assert(func != nullptr);

    return (t->*func)(L);
}

template <class T>
int invoke_const_member_cfunction(lua_State* L)
{
    using F = int (T::*)(lua_State * L) const;

    assert(isfulluserdata(L, lua_upvalueindex(1)));

    T const* const t = Userdata::get<T>(L, 1, true);
    
    F const& func = *static_cast<F const*>(lua_touserdata(L, lua_upvalueindex(1)));
    assert(func != nullptr);
    
    return (t->*func)(L);
}

//--------------------------------------------------------------------------
/**
    lua_CFunction to call on a object.

    The proxy function pointer (lightuserdata) is in the first upvalue.
    The class userdata object is at the top of the Lua stack.
*/
template <class F>
int invoke_proxy_function(lua_State* L)
{
    using FnTraits = detail::function_traits<F>;
    
    assert(lua_islightuserdata(L, lua_upvalueindex(1)));

    auto func = reinterpret_cast<F>(lua_touserdata(L, lua_upvalueindex(1)));
    assert(func != nullptr);

    return dispatcher<1, typename FnTraits::result_type, typename FnTraits::argument_types>::call(L, func);
}

template <class F>
int invoke_proxy_functor(lua_State* L)
{
    using FnTraits = detail::function_traits<F>;
    
    assert(isfulluserdata(L, lua_upvalueindex(1)));

    auto& func = *align<F>(lua_touserdata(L, lua_upvalueindex(1)));

    return dispatcher<1, typename FnTraits::result_type, typename FnTraits::argument_types>::call(L, func);
}

} // namespace detail
} // namespace luabridge
