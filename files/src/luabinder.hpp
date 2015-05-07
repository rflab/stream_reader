﻿#ifndef _RF_LUA_BINER_
#define _RF_LUA_BINER_

#include <stdio.h>

#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <sstream>
#include <type_traits>

#include "lua.hpp"

namespace rf
{
	class LuaBinder
	{
	private:
		lua_State* L_;

	public:
		bool open()
		{
			L_ = luaL_newstate(); // コンテキスト作成 
			luaL_openlibs(L_); // Lua標準ライブラリ読み込み
			return true;
		}

		bool close()
		{
			lua_close(L_);
			return true;
		}
		
		LuaBinder(){
			open();
		}

		~LuaBinder(){
			close();
		}

		void dump_stack()
		{
			int stack_size = lua_gettop(L_);
			printf("------------------------------------------\n");
			for (int i = 0; i < stack_size; i++)
			{
				int type = lua_type(L_, stack_size - i);
				printf("Stack[%2d(%3d) %10s] : ", stack_size - i, -i - 1, lua_typename(L_, type));

				switch (type)
				{
				case LUA_TNUMBER:
					printf("%f\n", lua_tonumber(L_, stack_size - i));
					break;
				case LUA_TBOOLEAN:
					if (lua_toboolean(L_, stack_size - i))
						printf("true\n");
					else
						printf("false\n");
					break;
				case LUA_TSTRING:
					printf("%s\n", lua_tostring(L_, stack_size - i));
					break;
				case LUA_TNIL:
					printf("\n");
					break;
				default:
					printf("%s\n", lua_typename(L_, type));
					break;
				}
			}
			printf("------------------------------------------\n");
		}

		bool dofile(const std::string& filename)
		{
			std::stringstream ss;
			ss.str("");
			ss.clear();

			// int top = lua_gettop(L_);
			int top = 0;

			int ret = luaL_dofile(L_, filename.c_str());
			switch (ret)
			{
			case LUA_OK: break;
			case LUA_ERRRUN:    ss << "ERROR LUA RUNTIME \n"; break;
			case LUA_ERRSYNTAX: ss << "ERROR LUA SYNTAX \n";  break;
			case LUA_ERRMEM:    ss << "ERROR LUA MEM \n";     break;
			case LUA_ERRFILE:   ss << "ERROR LUA FILE \n";    break;
			default: ss << "ERROR LUA\n"; break;
			}

			// エラーメッセージ表示
			if (ret != LUA_OK)
			{
				ss << lua_tostring(L_, -1);
				std::cout << ss.str() << std::endl;
				return false;
			}

			// 呼び出し前のスタックに戻す
			lua_settop(L_, top);
			return true;
		}

		//
		// 関数バインダー
		//

		// オーバーロードでC++->Lua
		template<typename T>
		static void push_stack(lua_State* L, T a)
		{
			lua_pushnumber(L, static_cast<lua_Number>(a));
		}

		static void push_stack(lua_State* L, bool a)
		{
			lua_pushboolean(L, a);
		}

		static void push_stack(lua_State* L, char const* a)
		{
			lua_pushstring(L, a);
		}

		static void push_stack(lua_State* L, const std::string& a)
		{
			lua_pushstring(L, a.c_str());
		}

		// 型推論でLua->C++
		// booleanがうまくいってない予感

		template<typename T>
		struct is_boolean
		{
			static const bool value =
				std::is_same < T, bool >::value;
		};

		template<typename T>
		struct is_number
		{
			static const bool value =
				!is_boolean<T>::value &&
				std::is_integral<T>::value ||
				std::is_floating_point<T>::value;
		};

		template<typename T>
		struct is_string
		{
			static const bool value =
				std::is_same<T, std::string>::value ||
				std::is_same<T, char const*>::value;
		};

		template<typename T>
		struct is_basic_type
		{
			static const bool value =
				is_boolean<T>::value ||
				is_number<T>::value ||
				is_string<T>::value;
		};

		template<typename T, typename std::enable_if<is_number<T>::value>::type* = 0>
		static T get_stack(lua_State* L, int index)
		{
			return static_cast<T>(lua_tonumber(L, index));
		}

		template<typename T, typename std::enable_if<is_boolean<T>::value>::type* = 0>
		static bool get_stack(lua_State* L, int index)
		{
			return lua_isboolean(L, index) == 0 ? false : true;
		}

		template<typename T, typename std::enable_if<is_string<T>::value>::type* = 0>
		static const char* get_stack(lua_State* L, int index)
		{
			return lua_tostring(L, index);
		}

		// 引数インデックス用シーケンス
		// integer_sequence<int, 1, 2, 3, 4>
		template<typename Tp, Tp ... N>
		struct intetger_sequence
		{};

		template<typename Vec, typename Tp>
		struct push_back;

		template<template<typename T, T ... V> class VecC,
			template<typename T, T V> class IntC,
			typename T, T ... V1, T V2>
		struct push_back < VecC<T, V1...>, IntC<T, V2> >
		{
			typedef VecC<T, V1..., V2> type;
		};

		template<typename T, T First, T Last>
		struct make_integral_sequence;

		template<typename T, T First, T Last>
		struct make_integral_sequence
		{
			typedef typename
				push_back < typename make_integral_sequence<T, First, Last - 1>::type,
				std::integral_constant<T, Last - 1 > > ::type type;
		};

		template<typename T, T A>
		struct make_integral_sequence < T, A, A >
		{
			typedef intetger_sequence<T> type;
		};

		template<typename T, T First, T Last>
		struct indexed_integral_sequence
		{
			static_assert(First <= Last, "");
			typedef typename make_integral_sequence<T, First, Last>::type type;
		};

		// 引数型情報とLuaから呼び出す関数をもつスタブのようなオブジェクト
		// 関数、void関数、メンバ関数、voidメンバ関数を特殊化する
		template<typename Func, typename Seq, typename IsMember = void>
		struct invoker;

		template<typename Ret, typename... Args, std::size_t ... Ixs>
		struct invoker < Ret(*)(Args...), intetger_sequence<size_t, Ixs...> >
		{
			static int apply(lua_State* L)
			{
				auto f = reinterpret_cast<Ret(*)(Args...)>(lua_tocfunction(L, lua_upvalueindex(1)));
				Ret r = f(get_stack<Args>(L, Ixs)...);
				LuaBinder::push_stack(L, r);
				return 1;
			}
		};

		template<typename... Args, std::size_t ... Ixs>
		struct invoker < void(*)(Args...), intetger_sequence<size_t, Ixs...> >
		{
			static int apply(lua_State* L)
			{
				auto f = reinterpret_cast<void(*)(Args...)>(lua_tocfunction(L, lua_upvalueindex(1)));
				f(get_stack<Args>(L, Ixs)...);
				return 0;
			}
		};

		template<class T, typename Ret, typename ... Args, std::size_t ... Ixs>
		struct invoker< Ret(T::*)(Args...), intetger_sequence<size_t, Ixs...>,
			typename std::enable_if<std::is_member_function_pointer<Ret(T::*)(Args...)>::value>::type>
		{
			static int apply(lua_State* L)
			{
				auto self = static_cast<T*>(lua_touserdata(L, 1));
				if (self == nullptr){
					cout << "no self specified" << endl;
				}

				// これで通ったけどなんでかなー
				typedef typename std::remove_reference<Ret(T::*)(Args...)>::type mf_type;
				void* buf = lua_touserdata(L, lua_upvalueindex(1));
				auto a = static_cast<std::array<mf_type, 1>*> (buf);
				mf_type fp = (*a)[0];

				Ret r = (self->*fp)(get_stack<Args>(L, Ixs)...);
				push_stack(L, r);
				return 1;
			}
		};


		template<class T, typename ... Args, std::size_t ... Ixs>
		struct invoker< void(T::*)(Args...), intetger_sequence<size_t, Ixs...>,
			typename std::enable_if<std::is_member_function_pointer<void(T::*)(Args...)>::value>::type>
		{
			static int apply(lua_State* L)
			{
				auto self = static_cast<T*>(lua_touserdata(L, 1));
				if (self == nullptr){
					cout << "no self specified" << endl;
				}

				// これで通ったけどなんでかなー
				typedef typename std::remove_reference<void(T::*)(Args...)>::type mf_type;
				void* buf = lua_touserdata(L, lua_upvalueindex(1));
				auto a = static_cast<std::array<mf_type, 1>*> (buf);
				mf_type fp = (*a)[0];

				r = (self->*fp)(get_stack<Args>(L, Ixs)...);
				push_stack(L, r);
				return 0;
			}
		};

		//----------------------------------------
		// 関数

		// 関数を登録
		template<typename R, typename ... Args>
		void def(std::string const& func_name, R(*f)(Args...))
		{
			typedef make_integral_sequence<std::size_t, 1, sizeof...(Args)+1>::type seq;
			lua_CFunction upvalue = invoker<decltype(f), seq>::apply;

			// 登録する関数はinvokerから呼び出すので関数型にキャストしてクロージャに入れる
			lua_pushcfunction(L_, reinterpret_cast<lua_CFunction>(f));
			lua_pushcclosure(L_, upvalue, 1);
			lua_setglobal(L_, func_name.c_str());
		}

		//----------------------------------------
		// クラス

		// 引数なしコンストラクタ
		template<class T>
		static int new_instance(lua_State* L)
		{
			void *p = lua_newuserdata(L, sizeof(T));
			int userdata = lua_gettop(L);
			void *instance = new(p)T;

			lua_pushvalue(L, lua_upvalueindex(1)); // クラスを取り出す
			lua_setmetatable(L, userdata); // メタテーブルに追加する

			return 1; // インスタンス1つを返す
		}

		template<class T>
		static int delete_instance(lua_State* L)
		{
			// ポインタ指定の配置newなのでdeleteしない
			T* instance = static_cast<T*>(lua_touserdata(L, -1));
			instance->~T();
			return 0;
		}

		template<class T>
		class class_chain
		{
		private:
			std::string name_;
			lua_State* L_;

		public:
			class_chain(lua_State* L, std::string const& name)
				:name_(name), L_(L)
			{
				// local table = {}
				lua_newtable(L_);
				int table = lua_gettop(L_);

				// レジストリ上にメタテーブルを登録し(重複の場合は無視)
				// スタックにそのテーブルを乗せる
				luaL_newmetatable(L_, name.c_str());
				int metatable = lua_gettop(L_);

				//getmetatableの動作を変更したい場合
				// // metatable[__metatable] = metatable
				// lua_pushliteral(L_, "__metatable");
				// lua_pushvalue(L_, metatable);
				// lua_settable(L_, metatable);

				// metatable[__index] = table
				lua_pushliteral(L_, "__index");
				lua_pushvalue(L_, table);
				lua_settable(L_, metatable);

				// metatable[__gc] = delete
				lua_pushliteral(L_, "__gc");
				lua_pushcfunction(L_, &delete_instance<T>);
				lua_settable(L_, metatable);

				// table.new = new(metatable)
				lua_pushliteral(L_, "new");
				lua_pushvalue(L_, metatable);
				lua_pushcclosure(L_, new_instance<T>, 1);
				lua_settable(L_, table);

				// _G[name] = table
				lua_pushvalue(L_, table);
				lua_setglobal(L_, name.c_str());

				// スタッククリア
				lua_pop(L_, 2);
			}

			template<typename Ret, typename... Args,
				typename std::enable_if<std::is_member_function_pointer<Ret(T::*)(Args...)>::value>::type* = 0>
				const class_chain<T>& def(std::string const& method_name, Ret(T::*f)(Args...)) const
			{
				typedef make_integral_sequence<std::size_t, 2, sizeof...(Args)+2>::type seq;
				lua_CFunction upvalue = invoker<Ret(T::*)(Args...), seq>::apply;

				luaL_getmetatable(L_, name_.c_str());
				int metatable = lua_gettop(L_);

				lua_getfield(L_, metatable, "__index");
				int methodtable = lua_gettop(L_);

				lua_pushstring(L_, method_name.c_str());

				// メンバ関数は実態としてしかコピー出来ない
				// （多分メンバ関数ポインタのサイズはsizeof(int)ではないから)
				// つまり↓は通らない
				// auto fpp = reinterpret_cast<void*>(f); 
				// なので配列として渡す
				typedef typename std::remove_reference<Ret(T::*)(Args...)>::type mf_type;
				void* buf = lua_newuserdata(L_, sizeof(std::array<mf_type, 1>));
				auto a = static_cast<std::array<mf_type, 1>*>(buf);
				(*a)[0] = f;

				lua_pushcclosure(L_, upvalue, 1);
				lua_settable(L_, methodtable);

				lua_pop(L_, 2);

				return *this;
			}
		};

		template<class T>
		std::shared_ptr<class_chain<T> > def_class(std::string const& name)
		{
			return make_shared<class_chain<T> >(L_, name);
		}
	};
}

#endif