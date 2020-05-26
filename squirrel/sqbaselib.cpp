/*
    see copyright notice in squirrel.h
*/
#include "sqpcheader.h"
#include "sqvm.h"
#include "sqstring.h"
#include "sqtable.h"
#include "sqarray.h"
#include "sqfuncproto.h"
#include "sqclosure.h"
#include "sqclass.h"
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string>
#include <algorithm>

static bool str2num(const SQChar *s,SQObjectPtr &res,SQInteger base)
{
    SQChar *end;
    const SQChar *e = s;
    bool iseintbase = base > 13; //to fix error converting hexadecimals with e like 56f0791e
    bool isfloat = false;
    SQChar c;
    while((c = *e) != _SC('\0'))
    {
        if (c == _SC('.') || (!iseintbase && (c == _SC('E') || c == _SC('e')))) { //e and E is for scientific notation
            isfloat = true;
            break;
        }
        e++;
    }
    if(isfloat){
        SQFloat r = SQFloat(scstrtod(s,&end));
        if(s == end) return false;
        res = r;
    }
    else{
        SQInteger r = SQInteger(scstrtol(s,&end,(int)base));
        if(s == end) return false;
        res = r;
    }
    return true;
}

static SQInteger base_dummy(HSQUIRRELVM SQ_UNUSED_ARG(v))
{
    return 0;
}

#ifndef NO_GARBAGE_COLLECTOR
static SQInteger base_collectgarbage(HSQUIRRELVM v)
{
    sq_pushinteger(v, sq_collectgarbage(v));
    return 1;
}
static SQInteger base_resurectureachable(HSQUIRRELVM v)
{
    sq_resurrectunreachable(v);
    return 1;
}
#endif

static SQInteger base_getroottable(HSQUIRRELVM v)
{
    v->Push(v->_roottable);
    return 1;
}

static SQInteger base_getconsttable(HSQUIRRELVM v)
{
    v->Push(_ss(v)->_consts);
    return 1;
}


static SQInteger base_setroottable(HSQUIRRELVM v)
{
    SQObjectPtr o = v->_roottable;
    if(SQ_FAILED(sq_setroottable(v))) return SQ_ERROR;
    v->Push(o);
    return 1;
}

static SQInteger base_setconsttable(HSQUIRRELVM v)
{
    SQObjectPtr o = _ss(v)->_consts;
    if(SQ_FAILED(sq_setconsttable(v))) return SQ_ERROR;
    v->Push(o);
    return 1;
}

static SQInteger base_seterrorhandler(HSQUIRRELVM v)
{
    sq_seterrorhandler(v);
    return 0;
}

static SQInteger base_setdebughook(HSQUIRRELVM v)
{
    sq_setdebughook(v);
    return 0;
}

static SQInteger base_enabledebuginfo(HSQUIRRELVM v)
{
    SQObjectPtr &o=stack_get(v,2);

    sq_enabledebuginfo(v,SQVM::IsFalse(o)?SQFalse:SQTrue);
    return 0;
}

static SQInteger __getcallstackinfos(HSQUIRRELVM v,SQInteger level)
{
    SQStackInfos si;
    SQInteger seq = 0;
    const SQChar *name = NULL;

    if (SQ_SUCCEEDED(sq_stackinfos(v, level, &si)))
    {
        const SQChar *fn = _SC("unknown");
        const SQChar *src = _SC("unknown");
        if(si.funcname)fn = si.funcname;
        if(si.source)src = si.source;
        sq_newtable(v);
        sq_pushstring(v, _SC("func"), -1);
        sq_pushstring(v, fn, -1);
        sq_newslot(v, -3, SQFalse);
        sq_pushstring(v, _SC("src"), -1);
        sq_pushstring(v, src, -1);
        sq_newslot(v, -3, SQFalse);
        sq_pushstring(v, _SC("line"), -1);
        sq_pushinteger(v, si.line);
        sq_newslot(v, -3, SQFalse);
        sq_pushstring(v, _SC("locals"), -1);
        sq_newtable(v);
        seq=0;
        while ((name = sq_getlocal(v, level, seq))) {
            sq_pushstring(v, name, -1);
            sq_push(v, -2);
            sq_newslot(v, -4, SQFalse);
            sq_pop(v, 1);
            seq++;
        }
        sq_newslot(v, -3, SQFalse);
        return 1;
    }

    return 0;
}
static SQInteger base_getstackinfos(HSQUIRRELVM v)
{
    SQInteger level;
    sq_getinteger(v, -1, &level);
    return __getcallstackinfos(v,level);
}

static SQInteger base_assert(HSQUIRRELVM v)
{
    if(SQVM::IsFalse(stack_get(v,2))){
        return sq_throwerror(v,_SC("assertion failed"));
    }
    return 0;
}

static SQInteger get_slice_params(HSQUIRRELVM v,SQInteger &sidx,SQInteger &eidx,SQObjectPtr &o)
{
    SQInteger top = sq_gettop(v);
    sidx=0;
    eidx=0;
    o=stack_get(v,1);
    if(top>1){
        SQObjectPtr &start=stack_get(v,2);
        if(type(start)!=OT_NULL && sq_isnumeric(start)){
            sidx=tointeger(start);
        }
    }
    if(top>2){
        SQObjectPtr &end=stack_get(v,3);
        if(sq_isnumeric(end)){
            eidx=tointeger(end);
        }
    }
    else {
        eidx = sq_getsize(v,1);
    }
    return 1;
}

static SQInteger base_print(HSQUIRRELVM v)
{
    const SQChar *str;
    if(SQ_SUCCEEDED(sq_tostring(v,2)))
    {
        if(SQ_SUCCEEDED(sq_getstring(v,-1,&str))) {
            if(_ss(v)->_printfunc) _ss(v)->_printfunc(v,_SC("%s"),str);
            return 0;
        }
    }
    return SQ_ERROR;
}

static SQInteger base_error(HSQUIRRELVM v)
{
    const SQChar *str;
    if(SQ_SUCCEEDED(sq_tostring(v,2)))
    {
        if(SQ_SUCCEEDED(sq_getstring(v,-1,&str))) {
            if(_ss(v)->_errorfunc) _ss(v)->_errorfunc(v,_SC("%s"),str);
            return 0;
        }
    }
    return SQ_ERROR;
}

static SQInteger base_compilestring(HSQUIRRELVM v)
{
    SQInteger nargs=sq_gettop(v);
    const SQChar *src=NULL,*name=_SC("unnamedbuffer");
    SQInteger size;
    sq_getstring(v,2,&src);
    size=sq_getsize(v,2);
    if(nargs>2){
        sq_getstring(v,3,&name);
    }
    if(SQ_SUCCEEDED(sq_compilebuffer(v,src,size,name,SQFalse)))
        return 1;
    else
        return SQ_ERROR;
}

static SQInteger base_newthread(HSQUIRRELVM v)
{
    SQObjectPtr &func = stack_get(v,2);
    SQInteger stksize = (_closure(func)->_function->_stacksize << 1) +2;
    HSQUIRRELVM newv = sq_newthread(v, (stksize < MIN_STACK_OVERHEAD + 2)? MIN_STACK_OVERHEAD + 2 : stksize);
    sq_move(newv,v,-2);
    return 1;
}

static SQInteger base_suspend(HSQUIRRELVM v)
{
    return sq_suspendvm(v);
}

static SQInteger base_array(HSQUIRRELVM v)
{
    SQArray *a;
    SQObject &size = stack_get(v,2);
    if(sq_gettop(v) > 2) {
        a = SQArray::Create(_ss(v),0);
        a->Resize(tointeger(size),stack_get(v,3));
    }
    else {
        a = SQArray::Create(_ss(v),tointeger(size));
    }
    v->Push(a);
    return 1;
}

static SQInteger base_type(HSQUIRRELVM v)
{
    SQObjectPtr &o = stack_get(v,2);
    v->Push(SQString::Create(_ss(v),GetTypeName(o),-1));
    return 1;
}

static SQInteger base_callee(HSQUIRRELVM v)
{
    if(v->_callsstacksize > 1)
    {
        v->Push(v->_callsstack[v->_callsstacksize - 2]._closure);
        return 1;
    }
    return sq_throwerror(v,_SC("no closure in the calls stack"));
}

static const SQRegFunction base_funcs[]={
    //generic
    {_SC("seterrorhandler"),base_seterrorhandler,2, NULL},
    {_SC("setdebughook"),base_setdebughook,2, NULL},
    {_SC("enabledebuginfo"),base_enabledebuginfo,2, NULL},
    {_SC("getstackinfos"),base_getstackinfos,2, _SC(".n")},
    {_SC("getroottable"),base_getroottable,1, NULL},
    {_SC("setroottable"),base_setroottable,2, NULL},
    {_SC("getconsttable"),base_getconsttable,1, NULL},
    {_SC("setconsttable"),base_setconsttable,2, NULL},
    {_SC("assert"),base_assert,2, NULL},
    {_SC("print"),base_print,2, NULL},
    {_SC("error"),base_error,2, NULL},
    {_SC("compilestring"),base_compilestring,-2, _SC(".ss")},
    {_SC("newthread"),base_newthread,2, _SC(".c")},
    {_SC("suspend"),base_suspend,-1, NULL},
    {_SC("array"),base_array,-2, _SC(".n")},
    {_SC("type"),base_type,2, NULL},
    {_SC("callee"),base_callee,0,NULL},
    {_SC("dummy"),base_dummy,0,NULL},
#ifndef NO_GARBAGE_COLLECTOR
    {_SC("collectgarbage"),base_collectgarbage,0, NULL},
    {_SC("resurrectunreachable"),base_resurectureachable,0, NULL},
#endif
    {NULL,(SQFUNCTION)0,0,NULL}
};

void sq_base_register(HSQUIRRELVM v)
{
    SQInteger i=0;
    sq_pushroottable(v);
    while(base_funcs[i].name!=0) {
        sq_pushstring(v,base_funcs[i].name,-1);
        sq_newclosure(v,base_funcs[i].f,0);
        sq_setnativeclosurename(v,-1,base_funcs[i].name);
        sq_setparamscheck(v,base_funcs[i].nparamscheck,base_funcs[i].typemask);
        sq_newslot(v,-3, SQFalse);
        i++;
    }

    sq_pushstring(v,_SC("_versionnumber_"),-1);
    sq_pushinteger(v,SQUIRREL_VERSION_NUMBER);
    sq_newslot(v,-3, SQFalse);
    sq_pushstring(v,_SC("_version_"),-1);
    sq_pushstring(v,SQUIRREL_VERSION,-1);
    sq_newslot(v,-3, SQFalse);
    sq_pushstring(v,_SC("_charsize_"),-1);
    sq_pushinteger(v,sizeof(SQChar));
    sq_newslot(v,-3, SQFalse);
    sq_pushstring(v,_SC("_intsize_"),-1);
    sq_pushinteger(v,sizeof(SQInteger));
    sq_newslot(v,-3, SQFalse);
    sq_pushstring(v,_SC("_floatsize_"),-1);
    sq_pushinteger(v,sizeof(SQFloat));
    sq_newslot(v,-3, SQFalse);
    sq_pop(v,1);
}

static SQInteger default_delegate_len(HSQUIRRELVM v)
{
    v->Push(SQInteger(sq_getsize(v,1)));
    return 1;
}

static SQInteger default_delegate_tofloat(HSQUIRRELVM v)
{
    SQObjectPtr &o=stack_get(v,1);
    switch(type(o)){
    case OT_STRING:{
        SQObjectPtr res;
        if(str2num(_stringval(o),res,10)){
            v->Push(SQObjectPtr(tofloat(res)));
            break;
        }}
        return sq_throwerror(v, _SC("cannot convert the string"));
        break;
    case OT_INTEGER:case OT_FLOAT:
        v->Push(SQObjectPtr(tofloat(o)));
        break;
    case OT_BOOL:
        v->Push(SQObjectPtr((SQFloat)(_integer(o)?1:0)));
        break;
    default:
        v->PushNull();
        break;
    }
    return 1;
}

static SQInteger default_delegate_tointeger(HSQUIRRELVM v)
{
    SQObjectPtr &o=stack_get(v,1);
    SQInteger base = 10;
    if(sq_gettop(v) > 1) {
        sq_getinteger(v,2,&base);
    }
    switch(type(o)){
    case OT_STRING:{
        SQObjectPtr res;
        if(str2num(_stringval(o),res,base)){
            v->Push(SQObjectPtr(tointeger(res)));
            break;
        }}
        return sq_throwerror(v, _SC("cannot convert the string"));
        break;
    case OT_INTEGER:case OT_FLOAT:
        v->Push(SQObjectPtr(tointeger(o)));
        break;
    case OT_BOOL:
        v->Push(SQObjectPtr(_integer(o)?(SQInteger)1:(SQInteger)0));
        break;
    default:
        v->PushNull();
        break;
    }
    return 1;
}

static SQInteger default_delegate_tostring(HSQUIRRELVM v)
{
    if(SQ_FAILED(sq_tostring(v,1)))
        return SQ_ERROR;
    return 1;
}

static SQInteger obj_delegate_weakref(HSQUIRRELVM v)
{
    sq_weakref(v,1);
    return 1;
}

static SQInteger obj_clear(HSQUIRRELVM v)
{
    return sq_clear(v,-1);
}


static SQInteger number_delegate_tochar(HSQUIRRELVM v)
{
    SQObject &o=stack_get(v,1);
    SQChar c = (SQChar)tointeger(o);
    v->Push(SQString::Create(_ss(v),(const SQChar *)&c,1));
    return 1;
}



/////////////////////////////////////////////////////////////////
//TABLE DEFAULT DELEGATE

static SQInteger table_rawdelete(HSQUIRRELVM v)
{
    if(SQ_FAILED(sq_rawdeleteslot(v,1,SQTrue)))
        return SQ_ERROR;
    return 1;
}


static SQInteger container_rawexists(HSQUIRRELVM v)
{
    if(SQ_SUCCEEDED(sq_rawget(v,-2))) {
        sq_pushbool(v,SQTrue);
        return 1;
    }
    sq_pushbool(v,SQFalse);
    return 1;
}

static SQInteger container_rawset(HSQUIRRELVM v)
{
    return sq_rawset(v,-3);
}


static SQInteger container_rawget(HSQUIRRELVM v)
{
    return SQ_SUCCEEDED(sq_rawget(v,-2))?1:SQ_ERROR;
}

static SQInteger table_setdelegate(HSQUIRRELVM v)
{
    if(SQ_FAILED(sq_setdelegate(v,-2)))
        return SQ_ERROR;
    sq_push(v,-1); // -1 because sq_setdelegate pops 1
    return 1;
}

static SQInteger table_getdelegate(HSQUIRRELVM v)
{
    return SQ_SUCCEEDED(sq_getdelegate(v,-1))?1:SQ_ERROR;
}

static std::string _array_reverse(SQArray* arr, SQInteger indent = 0, SQInteger separator = 0);
static std::string _table_reverse(SQTable* tbl, SQInteger indent = 0, SQInteger separator = 0);

static std::string _obj_to_str(const SQObjectPtr& obj, SQInteger indent = 0, SQInteger separator = 0, SQBool escape = true)
{
	switch(type(obj))
	{
		case OT_INTEGER:
		{
			return std::to_string(_integer(obj));
		}
		case OT_FLOAT:
		{
			return std::to_string(_float(obj));
		}
		case OT_BOOL:
		{
			return (_integer(obj) ? "true" : "false");
		}
		case OT_STRING:
		{
			if(!escape)
				return std::string(_stringval(obj));
			
			return "\"" + std::string(_stringval(obj)) + "\"";
		}
		case OT_TABLE:
		{
			return _table_reverse(_table(obj), indent > 0 ? indent + 1 : 0, separator);
		}
		case OT_ARRAY:
		{
			return _array_reverse(_array(obj), indent > 0 ? indent + 1 : 0, separator);
		}
	}
	
	return "null";
}

static std::string _table_reverse(SQTable* tbl, SQInteger indent, SQInteger separator)
{
	SQInteger idx;
	std::string ret;
	SQObjectPtr refidx, key, val;
	
	std::string indents, separators;
	if(indent > 0)
	{
		indents += "\n";
		for(int i = 0; i < indent; ++i)
			indents += "\t";
	}
	if(separator > 0)
	{
		for(int i = 0; i < separator; ++i)
			separators += " ";
	}
	
	while((idx = tbl->Next(false, refidx, key, val)) != -1)
	{
		std::string k = _obj_to_str(key, indent, separator), v = _obj_to_str(val, indent, separator);
		
		if(ret.empty())
			ret = k + separators + ":" + separators + v;
		else
			ret += "," + indents + k + separators + ":" + separators + v;
		
		refidx = idx;
	}
	
	return "{" + indents + ret + indents + "}";
}

static SQInteger table_tostring(HSQUIRRELVM v)
{
	SQTable* tbl = _table(stack_get(v, 1));
	std::string ret = _table_reverse(tbl);
	v->Push(SQString::Create(_ss(v), ret.c_str(), ret.size()));
	return 1;
}

static SQInteger table_keys(HSQUIRRELVM v)
{
	SQTable* tbl = _table(stack_get(v, 1));
	SQArray* ret = SQArray::Create(_ss(v), tbl->CountUsed());
	
	SQInteger idx, i = 0;
	SQObjectPtr refidx, key, val;
	
	while((idx = tbl->Next(false, refidx, key, val)) != -1)
	{
		ret->Set(i++, key);
		refidx = idx;
	}
	
	v->Push(ret);
	return 1;
}

static SQInteger table_values(HSQUIRRELVM v)
{
	SQTable* tbl = _table(stack_get(v, 1));
	SQArray* ret = SQArray::Create(_ss(v), tbl->CountUsed());
	
	SQInteger idx, i = 0;
	SQObjectPtr refidx, key, val;
	
	while((idx = tbl->Next(false, refidx, key, val)) != -1)
	{
		ret->Set(i++, val);
		refidx = idx;
	}
	
	v->Push(ret);
	return 1;
}

static SQInteger table_update(HSQUIRRELVM v)
{
	SQTable* original = _table(stack_get(v, 1));
	SQTable* replace = _table(stack_get(v, 2));
	
	SQInteger idx, numReplaced = 0;
	SQObjectPtr refidx, key, val;
	while((idx = replace->Next(false, refidx, key, val)) != -1)
	{
		if(!original->NewSlot(key, val))
			numReplaced += 1;
		
		refidx = idx;
	}
	
	sq_pushinteger(v, numReplaced);
	return 1;
}

const SQRegFunction SQSharedState::_table_default_delegate_funcz[]={
    {_SC("len"),default_delegate_len,1, _SC("t")},
    {_SC("rawget"),container_rawget,2, _SC("t")},
    {_SC("rawset"),container_rawset,3, _SC("t")},
    {_SC("rawdelete"),table_rawdelete,2, _SC("t")},
    {_SC("rawin"),container_rawexists,2, _SC("t")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
    {_SC("tostring"),table_tostring,1, _SC(".")},
    {_SC("clear"),obj_clear,1, _SC(".")},
    {_SC("setdelegate"),table_setdelegate,2, _SC(".t|o")},
    {_SC("getdelegate"),table_getdelegate,1, _SC(".")},
	{_SC("keys"),table_keys,1, _SC("t")},
	{_SC("values"),table_values,1, _SC("t")},
	{_SC("update"),table_update,2, _SC("tt")},
    {NULL,(SQFUNCTION)0,0,NULL}
};

//ARRAY DEFAULT DELEGATE///////////////////////////////////////

static SQInteger array_append(HSQUIRRELVM v)
{
    return sq_arrayappend(v,-2);
}

static SQInteger array_extend(HSQUIRRELVM v)
{
    _array(stack_get(v,1))->Extend(_array(stack_get(v,2)));
    return 0;
}

static SQInteger array_reverse(HSQUIRRELVM v)
{
    return sq_arrayreverse(v,-1);
}

static SQInteger array_pop(HSQUIRRELVM v)
{
    return SQ_SUCCEEDED(sq_arraypop(v,1,SQTrue))?1:SQ_ERROR;
}

static SQInteger array_top(HSQUIRRELVM v)
{
    SQObject &o=stack_get(v,1);
    if(_array(o)->Size()>0){
        v->Push(_array(o)->Top());
        return 1;
    }
    else return sq_throwerror(v,_SC("top() on a empty array"));
}

static SQInteger array_insert(HSQUIRRELVM v)
{
    SQObject &o=stack_get(v,1);
    SQObject &idx=stack_get(v,2);
    SQObject &val=stack_get(v,3);
    if(!_array(o)->Insert(tointeger(idx),val))
        return sq_throwerror(v,_SC("index out of range"));
    return 0;
}

static SQInteger array_remove(HSQUIRRELVM v)
{
    SQObject &o = stack_get(v, 1);
    SQObject &idx = stack_get(v, 2);
    if(!sq_isnumeric(idx)) return sq_throwerror(v, _SC("wrong type"));
    SQObjectPtr val;
    if(_array(o)->Get(tointeger(idx), val)) {
        _array(o)->Remove(tointeger(idx));
        v->Push(val);
        return 1;
    }
    return sq_throwerror(v, _SC("idx out of range"));
}

static SQInteger array_resize(HSQUIRRELVM v)
{
    SQObject &o = stack_get(v, 1);
    SQObject &nsize = stack_get(v, 2);
    SQObjectPtr fill;
    if(sq_isnumeric(nsize)) {
        if(sq_gettop(v) > 2)
            fill = stack_get(v, 3);
        _array(o)->Resize(tointeger(nsize),fill);
        return 0;
    }
    return sq_throwerror(v, _SC("size must be a number"));
}

static SQInteger __map_array(SQArray *dest,SQArray *src,HSQUIRRELVM v) {
    SQObjectPtr temp;
    SQInteger size = src->Size();
    for(SQInteger n = 0; n < size; n++) {
        src->Get(n,temp);
        v->Push(src);
        v->Push(temp);
        if(SQ_FAILED(sq_call(v,2,SQTrue,SQFalse))) {
            return SQ_ERROR;
        }
        dest->Set(n,v->GetUp(-1));
        v->Pop();
    }
    return 0;
}

static SQInteger array_map(HSQUIRRELVM v)
{
    SQObject &o = stack_get(v,1);
    SQInteger size = _array(o)->Size();
    SQObjectPtr ret = SQArray::Create(_ss(v),size);
    if(SQ_FAILED(__map_array(_array(ret),_array(o),v)))
        return SQ_ERROR;
    v->Push(ret);
    return 1;
}

static SQInteger array_apply(HSQUIRRELVM v)
{
    SQObject &o = stack_get(v,1);
    if(SQ_FAILED(__map_array(_array(o),_array(o),v)))
        return SQ_ERROR;
    return 0;
}

static SQInteger array_reduce(HSQUIRRELVM v)
{
    SQObject &o = stack_get(v,1);
    SQArray *a = _array(o);
    SQInteger size = a->Size();
    if(size == 0) {
        return 0;
    }
    SQObjectPtr res;
    a->Get(0,res);
    if(size > 1) {
        SQObjectPtr other;
        for(SQInteger n = 1; n < size; n++) {
            a->Get(n,other);
            v->Push(o);
            v->Push(res);
            v->Push(other);
            if(SQ_FAILED(sq_call(v,3,SQTrue,SQFalse))) {
                return SQ_ERROR;
            }
            res = v->GetUp(-1);
            v->Pop();
        }
    }
    v->Push(res);
    return 1;
}

static SQInteger array_filter(HSQUIRRELVM v)
{
    SQObject &o = stack_get(v,1);
    SQArray *a = _array(o);
    SQObjectPtr ret = SQArray::Create(_ss(v),0);
    SQInteger size = a->Size();
    SQObjectPtr val;
    for(SQInteger n = 0; n < size; n++) {
        a->Get(n,val);
        v->Push(o);
        v->Push(n);
        v->Push(val);
        if(SQ_FAILED(sq_call(v,3,SQTrue,SQFalse))) {
            return SQ_ERROR;
        }
        if(!SQVM::IsFalse(v->GetUp(-1))) {
            _array(ret)->Append(val);
        }
        v->Pop();
    }
    v->Push(ret);
    return 1;
}

static SQInteger array_find(HSQUIRRELVM v)
{
    SQObject &o = stack_get(v,1);
    SQObjectPtr &val = stack_get(v,2);
    SQArray *a = _array(o);
    SQInteger size = a->Size();
    SQObjectPtr temp;
    for(SQInteger n = 0; n < size; n++) {
        bool res = false;
        a->Get(n,temp);
        if(SQVM::IsEqual(temp,val,res) && res) {
            v->Push(n);
            return 1;
        }
    }
    return 0;
}


static bool _sort_compare(HSQUIRRELVM v,SQObjectPtr &a,SQObjectPtr &b,SQInteger func,SQInteger &ret)
{
    if(func < 0) {
        if(!v->ObjCmp(a,b,ret)) return false;
    }
    else {
        SQInteger top = sq_gettop(v);
        sq_push(v, func);
        sq_pushroottable(v);
        v->Push(a);
        v->Push(b);
        if(SQ_FAILED(sq_call(v, 3, SQTrue, SQFalse))) {
            if(!sq_isstring( v->_lasterror))
                v->Raise_Error(_SC("compare func failed"));
            return false;
        }
        if(SQ_FAILED(sq_getinteger(v, -1, &ret))) {
            v->Raise_Error(_SC("numeric value expected as return value of the compare function"));
            return false;
        }
        sq_settop(v, top);
        return true;
    }
    return true;
}

static bool _hsort_sift_down(HSQUIRRELVM v,SQArray *arr, SQInteger root, SQInteger bottom, SQInteger func)
{
    SQInteger maxChild;
    SQInteger done = 0;
    SQInteger ret;
    SQInteger root2;
    while (((root2 = root * 2) <= bottom) && (!done))
    {
        if (root2 == bottom) {
            maxChild = root2;
        }
        else {
            if(!_sort_compare(v,arr->_values[root2],arr->_values[root2 + 1],func,ret))
                return false;
            if (ret > 0) {
                maxChild = root2;
            }
            else {
                maxChild = root2 + 1;
            }
        }

        if(!_sort_compare(v,arr->_values[root],arr->_values[maxChild],func,ret))
            return false;
        if (ret < 0) {
            if (root == maxChild) {
                v->Raise_Error(_SC("inconsistent compare function"));
                return false; // We'd be swapping ourselve. The compare function is incorrect
            }

            _Swap(arr->_values[root],arr->_values[maxChild]);
            root = maxChild;
        }
        else {
            done = 1;
        }
    }
    return true;
}

static bool _hsort(HSQUIRRELVM v,SQObjectPtr &arr, SQInteger SQ_UNUSED_ARG(l), SQInteger SQ_UNUSED_ARG(r),SQInteger func)
{
    SQArray *a = _array(arr);
    SQInteger i;
    SQInteger array_size = a->Size();
    for (i = (array_size / 2); i >= 0; i--) {
        if(!_hsort_sift_down(v,a, i, array_size - 1,func)) return false;
    }

    for (i = array_size-1; i >= 1; i--)
    {
        _Swap(a->_values[0],a->_values[i]);
        if(!_hsort_sift_down(v,a, 0, i-1,func)) return false;
    }
    return true;
}

static SQInteger array_sort(HSQUIRRELVM v)
{
    SQInteger func = -1;
    SQObjectPtr &o = stack_get(v,1);
    if(_array(o)->Size() > 1) {
        if(sq_gettop(v) == 2) func = 2;
        if(!_hsort(v, o, 0, _array(o)->Size()-1, func))
            return SQ_ERROR;

    }
    return 0;
}

static SQInteger array_slice(HSQUIRRELVM v)
{
    SQInteger sidx,eidx;
    SQObjectPtr o;
    if(get_slice_params(v,sidx,eidx,o)==-1)return -1;
    SQInteger alen = _array(o)->Size();
    if(sidx < 0)sidx = alen + sidx;
    if(eidx < 0)eidx = alen + eidx;
    if(eidx < sidx)return sq_throwerror(v,_SC("wrong indexes"));
    if(eidx > alen || sidx < 0)return sq_throwerror(v, _SC("slice out of range"));
    SQArray *arr=SQArray::Create(_ss(v),eidx-sidx);
    SQObjectPtr t;
    SQInteger count=0;
    for(SQInteger i=sidx;i<eidx;i++){
        _array(o)->Get(i,t);
        arr->Set(count++,t);
    }
    v->Push(arr);
    return 1;

}

static std::string _array_reverse(SQArray* arr, SQInteger indent, SQInteger separator)
{
	SQInteger idx;
	std::string ret;
	SQObjectPtr refidx, key, val;
	
	std::string indents, separators;
	if(indent > 0)
	{
		indents += "\n";
		for(int i = 0; i < indent; ++i)
			indents += "\t";
	}
	if(separator > 0)
	{
		for(int i = 0; i < separator; ++i)
			separators += " ";
	}
	
	while((idx = arr->Next(refidx, key, val)) != -1)
	{
		std::string v = _obj_to_str(val, indent, separator);
		
		if(ret.empty())
			ret = v;
		else
			ret += "," + indents + separators + v;
		
		refidx = idx;
	}
	
	return "[" + indents + ret + indents + "]";
}

static SQInteger array_tostring(HSQUIRRELVM v)
{
	SQArray* arr = _array(stack_get(v, 1));
	std::string ret = _array_reverse(arr);
	v->Push(SQString::Create(_ss(v), ret.c_str(), ret.size()));
	return 1;
}

static SQInteger array_totable(HSQUIRRELVM v)
{
	SQArray* arr = _array(stack_get(v, 1));
	SQTable* ret = SQTable::Create(_ss(v), arr->Size());
	
	SQInteger idx, i = 0;
	SQObjectPtr refidx, key, val;
	while((idx = arr->Next(refidx, key, val)) != -1)
	{
		ret->NewSlot(key, val);
		refidx = idx;
	}
	
	v->Push(ret);
	return 1;
}

const SQRegFunction SQSharedState::_array_default_delegate_funcz[]={
    {_SC("len"),default_delegate_len,1, _SC("a")},
    {_SC("append"),array_append,2, _SC("a")},
    {_SC("extend"),array_extend,2, _SC("aa")},
    {_SC("push"),array_append,2, _SC("a")},
    {_SC("pop"),array_pop,1, _SC("a")},
    {_SC("top"),array_top,1, _SC("a")},
	{_SC("totable"),array_totable,1, _SC("a")},
    {_SC("insert"),array_insert,3, _SC("an")},
    {_SC("remove"),array_remove,2, _SC("an")},
    {_SC("resize"),array_resize,-2, _SC("an")},
    {_SC("reverse"),array_reverse,1, _SC("a")},
    {_SC("sort"),array_sort,-1, _SC("ac")},
    {_SC("slice"),array_slice,-1, _SC("ann")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
    {_SC("tostring"),array_tostring,1, _SC(".")},
    {_SC("clear"),obj_clear,1, _SC(".")},
    {_SC("map"),array_map,2, _SC("ac")},
    {_SC("apply"),array_apply,2, _SC("ac")},
    {_SC("reduce"),array_reduce,2, _SC("ac")},
    {_SC("filter"),array_filter,2, _SC("ac")},
    {_SC("find"),array_find,2, _SC("a.")},
    {NULL,(SQFUNCTION)0,0,NULL}
};

//STRING DEFAULT DELEGATE//////////////////////////
static SQInteger string_slice(HSQUIRRELVM v)
{
    SQInteger sidx,eidx;
    SQObjectPtr o;
    if(SQ_FAILED(get_slice_params(v,sidx,eidx,o)))return -1;
    SQInteger slen = _string(o)->_len;
    if(sidx < 0)sidx = slen + sidx;
    if(eidx < 0)eidx = slen + eidx;
    if(eidx < sidx) return sq_throwerror(v,_SC("wrong indexes"));
    if(eidx > slen || sidx < 0) return sq_throwerror(v, _SC("slice out of range"));
    v->Push(SQString::Create(_ss(v),&_stringval(o)[sidx],eidx-sidx));
    return 1;
}

static SQInteger string_find(HSQUIRRELVM v)
{
    SQInteger top,start_idx=0;
    const SQChar *str,*substr,*ret;
    if(((top=sq_gettop(v))>1) && SQ_SUCCEEDED(sq_getstring(v,1,&str)) && SQ_SUCCEEDED(sq_getstring(v,2,&substr))){
        if(top>2)sq_getinteger(v,3,&start_idx);
        if((sq_getsize(v,1)>start_idx) && (start_idx>=0)){
            ret=scstrstr(&str[start_idx],substr);
            if(ret){
                sq_pushinteger(v,(SQInteger)(ret-str));
                return 1;
            }
        }
        return 0;
    }
    return sq_throwerror(v,_SC("invalid param"));
}

static SQBool _string_tailmatch(const SQChar* str, const SQChar* sub, SQInteger start, SQInteger end, int direction)
{
	int selflen = (int)strlen(str);
	int slen = (int)strlen(sub);
	
	if (end > selflen)
        end = selflen;
    else if (end < 0) {
        end += selflen;
        if (end < 0)
        end = 0;
    }
    if (start < 0) {
        start += selflen;
        if (start < 0)
        start = 0;
    }
	
	if (direction < 0)
	{
		// startswith
		if (start + slen>selflen)
			return 0;
	}
	else
	{
		// endswith
		if (end - start < slen || start > selflen)
			return 0;
		if (end - slen > start)
			start = end - slen;
	}
	if (end - start >= slen)
		return !memcmp(str + start, sub, slen);
	return 0;
}

static SQInteger string_startswith(HSQUIRRELVM v)
{
    const SQChar *str, *substr;
	SQInteger top, start = 0, end = INT_MAX;
	if((top = sq_gettop(v)) > 1 && SQ_SUCCEEDED(sq_getstring(v, 1, &str)) && SQ_SUCCEEDED(sq_getstring(v, 2, &substr)))
	{
		if(top > 2) sq_getinteger(v, 3, &start);
		if(top > 3) sq_getinteger(v, 4, &end);
		sq_pushbool(v, _string_tailmatch(str, substr, start, end, -1));
		return 1;
	}
	
	return sq_throwerror(v,_SC("invalid param"));
}

static SQInteger string_endswith(HSQUIRRELVM v)
{
	const SQChar *str, *substr;
	SQInteger top, start = 0, end = INT_MAX;
	if((top = sq_gettop(v)) > 1 && SQ_SUCCEEDED(sq_getstring(v,1,&str)) && SQ_SUCCEEDED(sq_getstring(v,2,&substr)))
	{
		if(top > 2) sq_getinteger(v, 3, &start);
		if(top > 3) sq_getinteger(v, 4, &end);
		sq_pushbool(v, _string_tailmatch(str, substr, start, end, +1));
		return 1;
	}
	
	return sq_throwerror(v,_SC("invalid param"));
}

#define LEFTSTRIP 0
#define RIGHTSTRIP 1
#define BOTHSTRIP 2

static std::string _string_strip(const std::string& str, int striptype, const std::string& chars)
{
	std::string::size_type strlen = str.size();
	std::string::size_type charslen = chars.size();
	std::string::size_type i, j;
	
	const char*sep = chars.c_str();
	i = 0;
	if (striptype != RIGHTSTRIP)
	{
		while (i < strlen && memchr(sep, str[i], charslen))
		{
			i++;
		}
	}
	j = strlen;
	if (striptype != LEFTSTRIP)
	{
		j--;
		while (j >= i && memchr(sep, str[j], charslen))
		{
			j--;
		}
		j++;
	}
	
	if (0 == i&& j == strlen)
		return str;
	
	return str.substr(i, j - i);
}

static SQInteger string_strip(HSQUIRRELVM v)
{
	SQInteger top;
	const SQChar *str, *substr = " \r\n\t";
	if((top = sq_gettop(v)) > 0 && SQ_SUCCEEDED(sq_getstring(v,1,&str)))
	{
		if(top > 1) sq_getstring(v,2,&substr);
		std::string ret = _string_strip(str, BOTHSTRIP, substr);
		sq_pushstring(v, ret.c_str(), (SQInteger)ret.size());
		return 1;
	}
	
	return sq_throwerror(v,_SC("invalid param"));
}

static SQInteger string_lstrip(HSQUIRRELVM v)
{
	SQInteger top;
	const SQChar *str, *substr = " \r\n\t";
	if((top = sq_gettop(v)) > 0 && SQ_SUCCEEDED(sq_getstring(v,1,&str)))
	{
		if(top > 1) sq_getstring(v,2,&substr);
		std::string ret = _string_strip(str, LEFTSTRIP, substr);
		sq_pushstring(v, ret.c_str(), (SQInteger)ret.size());
		return 1;
	}
	
	return sq_throwerror(v,_SC("invalid param"));
}

static SQInteger string_rstrip(HSQUIRRELVM v)
{
	SQInteger top;
	const SQChar *str, *substr = " \r\n\t";
	if((top = sq_gettop(v)) > 0 && SQ_SUCCEEDED(sq_getstring(v,1,&str)))
	{
		if(top > 1) sq_getstring(v,2,&substr);
		std::string ret = _string_strip(str, RIGHTSTRIP, substr);
		sq_pushstring(v, ret.c_str(), (SQInteger)ret.size());
		return 1;
	}
	
	return sq_throwerror(v,_SC("invalid param"));
}

static SQInteger string_split(HSQUIRRELVM v)
{
    const SQChar *str, *substr;
	SQTable* ret = SQTable::Create(_ss(v), 0);
	if(sq_gettop(v) > 1 && SQ_SUCCEEDED(sq_getstring(v, 1, &str)) && SQ_SUCCEEDED(sq_getstring(v, 2, &substr)))
	{
		SQChar* clone = new SQChar[strlen(str) + 1];
		strcpy(clone, str);
		
		SQInteger i = 0;
		SQChar* nxt = strtok(clone, substr);
		while(nxt)
		{
			ret->NewSlot(i++, SQString::Create(_ss(v), nxt, -1));
			nxt = strtok(NULL, substr);
		}
		
		v->Push(ret);
		return 1;
	}
	
	return sq_throwerror(v,_SC("invalid param"));
}

static SQInteger string_rfind(HSQUIRRELVM v)
{
    SQInteger top,start_idx=0;
    const SQChar *str,*substr,*ret;
    if(((top=sq_gettop(v))>1) && SQ_SUCCEEDED(sq_getstring(v,1,&str)) && SQ_SUCCEEDED(sq_getstring(v,2,&substr))){
        if(top>2)sq_getinteger(v,3,&start_idx);
		
		auto lens = scstrlen(str), lend = scstrlen(substr);
		if(lend + start_idx > lens)
			return 0;
        
		for(const SQChar* last = str + lens - lend - start_idx; last >= str; --last)
		{
			if(!strncmp(last, substr, lend))
			{
				sq_pushinteger(v, (SQInteger)(last - str));
				return 1;
			}
		}
		
        return 0;
    }
    return sq_throwerror(v,_SC("invalid param"));
}

static SQInteger string_join(HSQUIRRELVM v)
{
	const SQChar *str;
	SQObjectPtr op;
	std::string ret;
	
	SQInteger idx;
	SQObjectPtr refidx, key, val;
	
	if(sq_gettop(v) > 1 && SQ_SUCCEEDED(sq_getstring(v,1,&str)) && type(op = stack_get(v, 2)) == OT_ARRAY)
	{
		while((idx = _array(op)->Next(refidx, key, val)) != -1)
		{
			if(ret.empty())
				ret = _obj_to_str(val, 0, 0, false);
			else
				ret += str + _obj_to_str(val, 0, 0, false);
			
			refidx = idx;
		}
		
		sq_pushstring(v, ret.c_str(), ret.size());
		return 1;
	}
	
	return sq_throwerror(v,_SC("invalid param"));
}

static SQInteger _string_count(const SQChar* str, const SQChar* sub)
{
	SQInteger count = 0;
	SQInteger klen = scstrlen(sub);
	const SQChar *pos_start = str, *pos_end;
	while(( pos_end = scstrstr(pos_start, sub) ))
	{
		pos_start = pos_end + klen;
		++count;
	}
	
	return count;
}

static SQInteger string_count(HSQUIRRELVM v)
{
	const SQChar *str, *sub;
	if(SQ_SUCCEEDED(sq_getstring(v,1,&str)) && SQ_SUCCEEDED(sq_getstring(v,2,&sub)))
	{
		sq_pushinteger(v, _string_count(str, sub));
		return 1;
	}
	
	return sq_throwerror(v,_SC("invalid param"));
}

static SQInteger string_replace(HSQUIRRELVM v)
{
	const SQChar *str, *rep, *to;
	if(SQ_SUCCEEDED(sq_getstring(v,1,&str)) && SQ_SUCCEEDED(sq_getstring(v,2,&rep)) && SQ_SUCCEEDED(sq_getstring(v,3,&to)))
	{
		SQInteger rep_len = scstrlen(rep);
		SQInteger to_len  = scstrlen(to);
		SQInteger counts = _string_count(str, rep);
		SQUnsignedInteger m = scstrlen(str) + counts * (to_len - rep_len) + 1;
		SQChar* new_buf = new SQChar[m];
		memset(new_buf, '\0', m);
		
		const SQChar *pos_start = str, *pos_end;
		SQChar *pbuf = new_buf;
		int copy_len;
		
		while(( pos_end = scstrstr(pos_start, rep) ))
		{
			copy_len = pos_end - pos_start;
			strncpy(pbuf, pos_start, copy_len);
			pbuf += copy_len;
			strcpy(pbuf, to);
			pbuf += to_len;
			pos_start  = pos_end + rep_len;
		}
		
		strcpy(pbuf, pos_start);
		v->Push(SQString::Create(_ss(v), new_buf));
		delete[] new_buf;
		return 1;
	}
	
	return sq_throwerror(v,_SC("invalid param"));
}

#define STRING_ISFUNCZ(func)	static SQInteger string_##func(HSQUIRRELVM v)	\
{\
	const SQChar *str;\
	sq_getstring(v,1,&str);\
	int len = strlen(str);\
	for(int i = 0; i < len; ++i) {\
		if(!func(str[i])) {\
			sq_pushbool(v, false);\
			return 1;\
		}\
	}\
	sq_pushbool(v, true);\
	return 1;\
}

STRING_ISFUNCZ(isalnum)
STRING_ISFUNCZ(isxdigit)
STRING_ISFUNCZ(isalpha)
STRING_ISFUNCZ(isascii)
STRING_ISFUNCZ(isdigit)
STRING_ISFUNCZ(isspace)
STRING_ISFUNCZ(islower)
STRING_ISFUNCZ(isupper)
STRING_ISFUNCZ(isprint)
STRING_ISFUNCZ(ispunct)

#define STRING_TOFUNCZ(func) static SQInteger string_##func(HSQUIRRELVM v) \
{\
    SQInteger sidx,eidx; \
    SQObjectPtr str; \
    if(SQ_FAILED(get_slice_params(v,sidx,eidx,str)))return -1; \
    SQInteger slen = _string(str)->_len; \
    if(sidx < 0)sidx = slen + sidx; \
    if(eidx < 0)eidx = slen + eidx; \
    if(eidx < sidx) return sq_throwerror(v,_SC("wrong indexes")); \
    if(eidx > slen || sidx < 0) return sq_throwerror(v,_SC("slice out of range")); \
    SQInteger len=_string(str)->_len; \
    const SQChar *sthis=_stringval(str); \
    SQChar *snew=(_ss(v)->GetScratchPad(sq_rsl(len))); \
    memcpy(snew,sthis,sq_rsl(len));\
    for(SQInteger i=sidx;i<eidx;i++) snew[i] = func(sthis[i]); \
    v->Push(SQString::Create(_ss(v),snew,len)); \
    return 1; \
}


STRING_TOFUNCZ(tolower)
STRING_TOFUNCZ(toupper)

const SQRegFunction SQSharedState::_string_default_delegate_funcz[]={
    {_SC("len"),default_delegate_len,1, _SC("s")},
    {_SC("tointeger"),default_delegate_tointeger,-1, _SC("sn")},
    {_SC("tofloat"),default_delegate_tofloat,1, _SC("s")},
    {_SC("tostring"),default_delegate_tostring,1, _SC(".")},
    {_SC("slice"),string_slice,-1, _SC("s n  n")},
    {_SC("find"),string_find,-2, _SC("s s n")},
	{_SC("rfind"),string_rfind,-2, _SC("s s n")},
    {_SC("tolower"),string_tolower,-1, _SC("s n n")},
    {_SC("toupper"),string_toupper,-1, _SC("s n n")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
	{_SC("endswith"),string_endswith,-2, _SC("ss n n") },
	{_SC("startswith"),string_startswith,-2, _SC("ss n n") },
	{_SC("strip"),string_strip,-1, _SC("s s") },
	{_SC("lstrip"),string_lstrip,-1, _SC("s s") },
	{_SC("rstrip"),string_rstrip,-1, _SC("s s") },
	{_SC("split"),string_split,2, _SC("ss") },
	{_SC("count"),string_count,2, _SC("ss") },
	{_SC("replace"),string_replace,3, _SC("sss") },
	{_SC("join"),string_join,2, _SC("sa") },
	{_SC("isalnum"),string_isalnum,1, _SC("s") },
	{_SC("isxdigit"),string_isxdigit,1, _SC("s") },
	{_SC("isalpha"),string_isalpha,1, _SC("s") },
	{_SC("isascii"),string_isascii,1, _SC("s") },
	{_SC("isdigit"),string_isdigit,1, _SC("s") },
	{_SC("isspace"),string_isspace,1, _SC("s") },
	{_SC("islower"),string_islower,1, _SC("s") },
	{_SC("isupper"),string_isupper,1, _SC("s") },
	{_SC("isprint"),string_isprint,1, _SC("s") },
	{_SC("ispunct"),string_ispunct,1, _SC("s") },
    {NULL,(SQFUNCTION)0,0,NULL}
};

//INTEGER DEFAULT DELEGATE//////////////////////////
const SQRegFunction SQSharedState::_number_default_delegate_funcz[]={
    {_SC("tointeger"),default_delegate_tointeger,1, _SC("n|b")},
    {_SC("tofloat"),default_delegate_tofloat,1, _SC("n|b")},
    {_SC("tostring"),default_delegate_tostring,1, _SC(".")},
    {_SC("tochar"),number_delegate_tochar,1, _SC("n|b")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
    {NULL,(SQFUNCTION)0,0,NULL}
};

//CLOSURE DEFAULT DELEGATE//////////////////////////
static SQInteger closure_pcall(HSQUIRRELVM v)
{
    return SQ_SUCCEEDED(sq_call(v,sq_gettop(v)-1,SQTrue,SQFalse))?1:SQ_ERROR;
}

static SQInteger closure_call(HSQUIRRELVM v)
{
    return SQ_SUCCEEDED(sq_call(v,sq_gettop(v)-1,SQTrue,SQTrue))?1:SQ_ERROR;
}

static SQInteger _closure_acall(HSQUIRRELVM v,SQBool raiseerror)
{
    SQArray *aparams=_array(stack_get(v,2));
    SQInteger nparams=aparams->Size();
    v->Push(stack_get(v,1));
    for(SQInteger i=0;i<nparams;i++)v->Push(aparams->_values[i]);
    return SQ_SUCCEEDED(sq_call(v,nparams,SQTrue,raiseerror))?1:SQ_ERROR;
}

static SQInteger closure_acall(HSQUIRRELVM v)
{
    return _closure_acall(v,SQTrue);
}

static SQInteger closure_pacall(HSQUIRRELVM v)
{
    return _closure_acall(v,SQFalse);
}

static SQInteger closure_bindenv(HSQUIRRELVM v)
{
    if(SQ_FAILED(sq_bindenv(v,1)))
        return SQ_ERROR;
    return 1;
}

static SQInteger closure_getroot(HSQUIRRELVM v)
{
    if(SQ_FAILED(sq_getclosureroot(v,-1)))
        return SQ_ERROR;
    return 1;
}

static SQInteger closure_setroot(HSQUIRRELVM v)
{
    if(SQ_FAILED(sq_setclosureroot(v,-2)))
        return SQ_ERROR;
    return 1;
}

static SQInteger closure_getinfos(HSQUIRRELVM v) {
    SQObject o = stack_get(v,1);
    SQTable *res = SQTable::Create(_ss(v),4);
    if(type(o) == OT_CLOSURE) {
        SQFunctionProto *f = _closure(o)->_function;
        SQInteger nparams = f->_nparameters + (f->_varparams?1:0);
        SQObjectPtr params = SQArray::Create(_ss(v),nparams);
    SQObjectPtr defparams = SQArray::Create(_ss(v),f->_ndefaultparams);
        for(SQInteger n = 0; n<f->_nparameters; n++) {
            _array(params)->Set((SQInteger)n,f->_parameters[n]);
        }
    for(SQInteger j = 0; j<f->_ndefaultparams; j++) {
            _array(defparams)->Set((SQInteger)j,_closure(o)->_defaultparams[j]);
        }
        if(f->_varparams) {
            _array(params)->Set(nparams-1,SQString::Create(_ss(v),_SC("..."),-1));
        }
        res->NewSlot(SQString::Create(_ss(v),_SC("native"),-1),false);
        res->NewSlot(SQString::Create(_ss(v),_SC("name"),-1),f->_name);
        res->NewSlot(SQString::Create(_ss(v),_SC("src"),-1),f->_sourcename);
        res->NewSlot(SQString::Create(_ss(v),_SC("parameters"),-1),params);
        res->NewSlot(SQString::Create(_ss(v),_SC("varargs"),-1),f->_varparams);
    res->NewSlot(SQString::Create(_ss(v),_SC("defparams"),-1),defparams);
    }
    else { //OT_NATIVECLOSURE
        SQNativeClosure *nc = _nativeclosure(o);
        res->NewSlot(SQString::Create(_ss(v),_SC("native"),-1),true);
        res->NewSlot(SQString::Create(_ss(v),_SC("name"),-1),nc->_name);
        res->NewSlot(SQString::Create(_ss(v),_SC("paramscheck"),-1),nc->_nparamscheck);
        SQObjectPtr typecheck;
        if(nc->_typecheck.size() > 0) {
            typecheck =
                SQArray::Create(_ss(v), nc->_typecheck.size());
            for(SQUnsignedInteger n = 0; n<nc->_typecheck.size(); n++) {
                    _array(typecheck)->Set((SQInteger)n,nc->_typecheck[n]);
            }
        }
        res->NewSlot(SQString::Create(_ss(v),_SC("typecheck"),-1),typecheck);
    }
    v->Push(res);
    return 1;
}



const SQRegFunction SQSharedState::_closure_default_delegate_funcz[]={
    {_SC("call"),closure_call,-1, _SC("c")},
    {_SC("pcall"),closure_pcall,-1, _SC("c")},
    {_SC("acall"),closure_acall,2, _SC("ca")},
    {_SC("pacall"),closure_pacall,2, _SC("ca")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
    {_SC("tostring"),default_delegate_tostring,1, _SC(".")},
    {_SC("bindenv"),closure_bindenv,2, _SC("c x|y|t")},
    {_SC("getinfos"),closure_getinfos,1, _SC("c")},
    {_SC("getroot"),closure_getroot,1, _SC("c")},
    {_SC("setroot"),closure_setroot,2, _SC("ct")},
    {NULL,(SQFUNCTION)0,0,NULL}
};

//GENERATOR DEFAULT DELEGATE
static SQInteger generator_getstatus(HSQUIRRELVM v)
{
    SQObject &o=stack_get(v,1);
    switch(_generator(o)->_state){
        case SQGenerator::eSuspended:v->Push(SQString::Create(_ss(v),_SC("suspended")));break;
        case SQGenerator::eRunning:v->Push(SQString::Create(_ss(v),_SC("running")));break;
        case SQGenerator::eDead:v->Push(SQString::Create(_ss(v),_SC("dead")));break;
    }
    return 1;
}

const SQRegFunction SQSharedState::_generator_default_delegate_funcz[]={
    {_SC("getstatus"),generator_getstatus,1, _SC("g")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
    {_SC("tostring"),default_delegate_tostring,1, _SC(".")},
    {NULL,(SQFUNCTION)0,0,NULL}
};

//THREAD DEFAULT DELEGATE
static SQInteger thread_call(HSQUIRRELVM v)
{
    SQObjectPtr o = stack_get(v,1);
    if(type(o) == OT_THREAD) {
        SQInteger nparams = sq_gettop(v);
        _thread(o)->Push(_thread(o)->_roottable);
        for(SQInteger i = 2; i<(nparams+1); i++)
            sq_move(_thread(o),v,i);
        if(SQ_SUCCEEDED(sq_call(_thread(o),nparams,SQTrue,SQTrue))) {
            sq_move(v,_thread(o),-1);
            sq_pop(_thread(o),1);
            return 1;
        }
        v->_lasterror = _thread(o)->_lasterror;
        return SQ_ERROR;
    }
    return sq_throwerror(v,_SC("wrong parameter"));
}

static SQInteger thread_wakeup(HSQUIRRELVM v)
{
    SQObjectPtr o = stack_get(v,1);
    if(type(o) == OT_THREAD) {
        SQVM *thread = _thread(o);
        SQInteger state = sq_getvmstate(thread);
        if(state != SQ_VMSTATE_SUSPENDED) {
            switch(state) {
                case SQ_VMSTATE_IDLE:
                    return sq_throwerror(v,_SC("cannot wakeup a idle thread"));
                break;
                case SQ_VMSTATE_RUNNING:
                    return sq_throwerror(v,_SC("cannot wakeup a running thread"));
                break;
            }
        }

        SQInteger wakeupret = sq_gettop(v)>1?SQTrue:SQFalse;
        if(wakeupret) {
            sq_move(thread,v,2);
        }
        if(SQ_SUCCEEDED(sq_wakeupvm(thread,wakeupret,SQTrue,SQTrue,SQFalse))) {
            sq_move(v,thread,-1);
            sq_pop(thread,1); //pop retval
            if(sq_getvmstate(thread) == SQ_VMSTATE_IDLE) {
                sq_settop(thread,1); //pop roottable
            }
            return 1;
        }
        sq_settop(thread,1);
        v->_lasterror = thread->_lasterror;
        return SQ_ERROR;
    }
    return sq_throwerror(v,_SC("wrong parameter"));
}

static SQInteger thread_wakeupthrow(HSQUIRRELVM v)
{
    SQObjectPtr o = stack_get(v,1);
    if(type(o) == OT_THREAD) {
        SQVM *thread = _thread(o);
        SQInteger state = sq_getvmstate(thread);
        if(state != SQ_VMSTATE_SUSPENDED) {
            switch(state) {
                case SQ_VMSTATE_IDLE:
                    return sq_throwerror(v,_SC("cannot wakeup a idle thread"));
                break;
                case SQ_VMSTATE_RUNNING:
                    return sq_throwerror(v,_SC("cannot wakeup a running thread"));
                break;
            }
        }

        sq_move(thread,v,2);
        sq_throwobject(thread);
        SQBool rethrow_error = SQTrue;
        if(sq_gettop(v) > 2) {
            sq_getbool(v,3,&rethrow_error);
        }
        if(SQ_SUCCEEDED(sq_wakeupvm(thread,SQFalse,SQTrue,SQTrue,SQTrue))) {
            sq_move(v,thread,-1);
            sq_pop(thread,1); //pop retval
            if(sq_getvmstate(thread) == SQ_VMSTATE_IDLE) {
                sq_settop(thread,1); //pop roottable
            }
            return 1;
        }
        sq_settop(thread,1);
        if(rethrow_error) {
            v->_lasterror = thread->_lasterror;
            return SQ_ERROR;
        }
        return SQ_OK;
    }
    return sq_throwerror(v,_SC("wrong parameter"));
}

static SQInteger thread_getstatus(HSQUIRRELVM v)
{
    SQObjectPtr &o = stack_get(v,1);
    switch(sq_getvmstate(_thread(o))) {
        case SQ_VMSTATE_IDLE:
            sq_pushstring(v,_SC("idle"),-1);
        break;
        case SQ_VMSTATE_RUNNING:
            sq_pushstring(v,_SC("running"),-1);
        break;
        case SQ_VMSTATE_SUSPENDED:
            sq_pushstring(v,_SC("suspended"),-1);
        break;
        default:
            return sq_throwerror(v,_SC("internal VM error"));
    }
    return 1;
}

static SQInteger thread_getstackinfos(HSQUIRRELVM v)
{
    SQObjectPtr o = stack_get(v,1);
    if(type(o) == OT_THREAD) {
        SQVM *thread = _thread(o);
        SQInteger threadtop = sq_gettop(thread);
        SQInteger level;
        sq_getinteger(v,-1,&level);
        SQRESULT res = __getcallstackinfos(thread,level);
        if(SQ_FAILED(res))
        {
            sq_settop(thread,threadtop);
            if(type(thread->_lasterror) == OT_STRING) {
                sq_throwerror(v,_stringval(thread->_lasterror));
            }
            else {
                sq_throwerror(v,_SC("unknown error"));
            }
        }
        if(res > 0) {
            //some result
            sq_move(v,thread,-1);
            sq_settop(thread,threadtop);
            return 1;
        }
        //no result
        sq_settop(thread,threadtop);
        return 0;

    }
    return sq_throwerror(v,_SC("wrong parameter"));
}

const SQRegFunction SQSharedState::_thread_default_delegate_funcz[] = {
    {_SC("call"), thread_call, -1, _SC("v")},
    {_SC("wakeup"), thread_wakeup, -1, _SC("v")},
    {_SC("wakeupthrow"), thread_wakeupthrow, -2, _SC("v.b")},
    {_SC("getstatus"), thread_getstatus, 1, _SC("v")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
    {_SC("getstackinfos"),thread_getstackinfos,2, _SC("vn")},
    {_SC("tostring"),default_delegate_tostring,1, _SC(".")},
    {NULL,(SQFUNCTION)0,0,NULL}
};

static SQInteger class_getattributes(HSQUIRRELVM v)
{
    return SQ_SUCCEEDED(sq_getattributes(v,-2))?1:SQ_ERROR;
}

static SQInteger class_setattributes(HSQUIRRELVM v)
{
    return SQ_SUCCEEDED(sq_setattributes(v,-3))?1:SQ_ERROR;
}

static SQInteger class_instance(HSQUIRRELVM v)
{
    return SQ_SUCCEEDED(sq_createinstance(v,-1))?1:SQ_ERROR;
}

static SQInteger class_getbase(HSQUIRRELVM v)
{
    return SQ_SUCCEEDED(sq_getbase(v,-1))?1:SQ_ERROR;
}

static SQInteger class_newmember(HSQUIRRELVM v)
{
    SQInteger top = sq_gettop(v);
    SQBool bstatic = SQFalse;
    if(top == 5)
    {
        sq_tobool(v,-1,&bstatic);
        sq_pop(v,1);
    }

    if(top < 4) {
        sq_pushnull(v);
    }
    return SQ_SUCCEEDED(sq_newmember(v,-4,bstatic))?1:SQ_ERROR;
}

static SQInteger class_rawnewmember(HSQUIRRELVM v)
{
    SQInteger top = sq_gettop(v);
    SQBool bstatic = SQFalse;
    if(top == 5)
    {
        sq_tobool(v,-1,&bstatic);
        sq_pop(v,1);
    }

    if(top < 4) {
        sq_pushnull(v);
    }
    return SQ_SUCCEEDED(sq_rawnewmember(v,-4,bstatic))?1:SQ_ERROR;
}

const SQRegFunction SQSharedState::_class_default_delegate_funcz[] = {
    {_SC("getattributes"), class_getattributes, 2, _SC("y.")},
    {_SC("setattributes"), class_setattributes, 3, _SC("y..")},
    {_SC("rawget"),container_rawget,2, _SC("y")},
    {_SC("rawset"),container_rawset,3, _SC("y")},
    {_SC("rawin"),container_rawexists,2, _SC("y")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
    {_SC("tostring"),default_delegate_tostring,1, _SC(".")},
    {_SC("instance"),class_instance,1, _SC("y")},
    {_SC("getbase"),class_getbase,1, _SC("y")},
    {_SC("newmember"),class_newmember,-3, _SC("y")},
    {_SC("rawnewmember"),class_rawnewmember,-3, _SC("y")},
    {NULL,(SQFUNCTION)0,0,NULL}
};


static SQInteger instance_getclass(HSQUIRRELVM v)
{
    if(SQ_SUCCEEDED(sq_getclass(v,1)))
        return 1;
    return SQ_ERROR;
}

const SQRegFunction SQSharedState::_instance_default_delegate_funcz[] = {
    {_SC("getclass"), instance_getclass, 1, _SC("x")},
    {_SC("rawget"),container_rawget,2, _SC("x")},
    {_SC("rawset"),container_rawset,3, _SC("x")},
    {_SC("rawin"),container_rawexists,2, _SC("x")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
    {_SC("tostring"),default_delegate_tostring,1, _SC(".")},
    {NULL,(SQFUNCTION)0,0,NULL}
};

static SQInteger weakref_ref(HSQUIRRELVM v)
{
    if(SQ_FAILED(sq_getweakrefval(v,1)))
        return SQ_ERROR;
    return 1;
}

const SQRegFunction SQSharedState::_weakref_default_delegate_funcz[] = {
    {_SC("ref"),weakref_ref,1, _SC("r")},
    {_SC("weakref"),obj_delegate_weakref,1, NULL },
    {_SC("tostring"),default_delegate_tostring,1, _SC(".")},
    {NULL,(SQFUNCTION)0,0,NULL}
};
