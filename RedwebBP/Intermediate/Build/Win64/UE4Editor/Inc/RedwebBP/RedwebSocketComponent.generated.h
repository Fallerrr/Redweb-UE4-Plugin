// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
struct FRedwebKeyValue;
#ifdef REDWEBBP_RedwebSocketComponent_generated_h
#error "RedwebSocketComponent.generated.h already included, missing '#pragma once' in RedwebSocketComponent.h"
#endif
#define REDWEBBP_RedwebSocketComponent_generated_h

#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_13_GENERATED_BODY \
	friend struct Z_Construct_UScriptStruct_FRedwebKeyValue_Statics; \
	REDWEBBP_API static class UScriptStruct* StaticStruct();


template<> REDWEBBP_API UScriptStruct* StaticStruct<struct FRedwebKeyValue>();

#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_25_DELEGATE \
struct _Script_RedwebBP_eventRedwebTypedMessageEvent_Parms \
{ \
	FString Type; \
	FString PayloadJson; \
}; \
static inline void FRedwebTypedMessageEvent_DelegateWrapper(const FMulticastScriptDelegate& RedwebTypedMessageEvent, const FString& Type, const FString& PayloadJson) \
{ \
	_Script_RedwebBP_eventRedwebTypedMessageEvent_Parms Parms; \
	Parms.Type=Type; \
	Parms.PayloadJson=PayloadJson; \
	RedwebTypedMessageEvent.ProcessMulticastDelegate<UObject>(&Parms); \
}


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_24_DELEGATE \
struct _Script_RedwebBP_eventRedwebRawMessageEvent_Parms \
{ \
	FString Message; \
}; \
static inline void FRedwebRawMessageEvent_DelegateWrapper(const FMulticastScriptDelegate& RedwebRawMessageEvent, const FString& Message) \
{ \
	_Script_RedwebBP_eventRedwebRawMessageEvent_Parms Parms; \
	Parms.Message=Message; \
	RedwebRawMessageEvent.ProcessMulticastDelegate<UObject>(&Parms); \
}


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_23_DELEGATE \
struct _Script_RedwebBP_eventRedwebErrorEvent_Parms \
{ \
	FString Error; \
}; \
static inline void FRedwebErrorEvent_DelegateWrapper(const FMulticastScriptDelegate& RedwebErrorEvent, const FString& Error) \
{ \
	_Script_RedwebBP_eventRedwebErrorEvent_Parms Parms; \
	Parms.Error=Error; \
	RedwebErrorEvent.ProcessMulticastDelegate<UObject>(&Parms); \
}


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_22_DELEGATE \
static inline void FRedwebConnectedEvent_DelegateWrapper(const FMulticastScriptDelegate& RedwebConnectedEvent) \
{ \
	RedwebConnectedEvent.ProcessMulticastDelegate<UObject>(NULL); \
}


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_SPARSE_DATA
#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_RPC_WRAPPERS \
 \
	DECLARE_FUNCTION(execSendHeartbeat); \
	DECLARE_FUNCTION(execIsConnected); \
	DECLARE_FUNCTION(execSendTypedFields); \
	DECLARE_FUNCTION(execSendJson); \
	DECLARE_FUNCTION(execSendRaw); \
	DECLARE_FUNCTION(execDisconnect); \
	DECLARE_FUNCTION(execConnect);


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_RPC_WRAPPERS_NO_PURE_DECLS \
 \
	DECLARE_FUNCTION(execSendHeartbeat); \
	DECLARE_FUNCTION(execIsConnected); \
	DECLARE_FUNCTION(execSendTypedFields); \
	DECLARE_FUNCTION(execSendJson); \
	DECLARE_FUNCTION(execSendRaw); \
	DECLARE_FUNCTION(execDisconnect); \
	DECLARE_FUNCTION(execConnect);


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesURedwebSocketComponent(); \
	friend struct Z_Construct_UClass_URedwebSocketComponent_Statics; \
public: \
	DECLARE_CLASS(URedwebSocketComponent, UActorComponent, COMPILED_IN_FLAGS(0 | CLASS_Config), CASTCLASS_None, TEXT("/Script/RedwebBP"), NO_API) \
	DECLARE_SERIALIZER(URedwebSocketComponent)


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_INCLASS \
private: \
	static void StaticRegisterNativesURedwebSocketComponent(); \
	friend struct Z_Construct_UClass_URedwebSocketComponent_Statics; \
public: \
	DECLARE_CLASS(URedwebSocketComponent, UActorComponent, COMPILED_IN_FLAGS(0 | CLASS_Config), CASTCLASS_None, TEXT("/Script/RedwebBP"), NO_API) \
	DECLARE_SERIALIZER(URedwebSocketComponent)


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_STANDARD_CONSTRUCTORS \
	/** Standard constructor, called after all reflected properties have been initialized */ \
	NO_API URedwebSocketComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()); \
	DEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(URedwebSocketComponent) \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, URedwebSocketComponent); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(URedwebSocketComponent); \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	NO_API URedwebSocketComponent(URedwebSocketComponent&&); \
	NO_API URedwebSocketComponent(const URedwebSocketComponent&); \
public:


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_ENHANCED_CONSTRUCTORS \
	/** Standard constructor, called after all reflected properties have been initialized */ \
	NO_API URedwebSocketComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()) : Super(ObjectInitializer) { }; \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	NO_API URedwebSocketComponent(URedwebSocketComponent&&); \
	NO_API URedwebSocketComponent(const URedwebSocketComponent&); \
public: \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, URedwebSocketComponent); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(URedwebSocketComponent); \
	DEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(URedwebSocketComponent)


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_PRIVATE_PROPERTY_OFFSET
#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_27_PROLOG
#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_GENERATED_BODY_LEGACY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_PRIVATE_PROPERTY_OFFSET \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_SPARSE_DATA \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_RPC_WRAPPERS \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_INCLASS \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_STANDARD_CONSTRUCTORS \
public: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


#define Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_PRIVATE_PROPERTY_OFFSET \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_SPARSE_DATA \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_RPC_WRAPPERS_NO_PURE_DECLS \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_INCLASS_NO_PURE_DECLS \
	Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h_30_ENHANCED_CONSTRUCTORS \
static_assert(false, "Unknown access specifier for GENERATED_BODY() macro in class RedwebSocketComponent."); \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


template<> REDWEBBP_API UClass* StaticClass<class URedwebSocketComponent>();

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID Gemhouse_Plugins_RedwebBP_Source_RedwebBP_Public_RedwebSocketComponent_h


PRAGMA_ENABLE_DEPRECATION_WARNINGS
