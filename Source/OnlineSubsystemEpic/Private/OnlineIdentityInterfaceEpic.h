#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemTypes.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystemEpicTypes.h"
#include "eos_sdk.h"

class FOnlineSubsystemEpic;

class FOnlineIdentityInterfaceEpic
	: public IOnlineIdentity
{
	FOnlineSubsystemEpic* SubsystemEpic;

	EOS_HConnect ConnectHandle;

	EOS_HAuth AuthHandle;

	EOS_NotificationId NotifyLoginStatusChangedId;

	EOS_NotificationId NotifyAuthExpiration;

	FOnlineIdentityInterfaceEpic() = delete;

	static void EOS_CALL EOS_Connect_OnLoginComplete(const EOS_Connect_LoginCallbackInfo* Data);
	static void EOS_CALL EOS_Connect_OnAuthExpiration(const EOS_Connect_AuthExpirationCallbackInfo* Data);
	static void EOS_CALL EOS_Connect_OnLoginStatusChanged(const EOS_Connect_LoginStatusChangedCallbackInfo* Data);
	static void EOS_CALL EOS_Auth_OnLoginComplete(const EOS_Auth_LoginCallbackInfo* Data);
	static void EOS_CALL EOS_Auth_OnLogoutComplete(const EOS_Auth_LogoutCallbackInfo* Data);
	static void EOS_CALL EOS_Connect_OnUserCreated(const EOS_Connect_CreateUserCallbackInfo* Data);
	static void EOS_CALL EOS_Connect_OnAccountLinked(const EOS_Connect_LinkAccountCallbackInfo* Data);

	TSharedPtr<FUserOnlineAccount> OnlineUserAccountFromPUID(EOS_ProductUserId const& PUID) const;
	ELoginStatus::Type EOSLoginStatusToUELoginStatus(EOS_ELoginStatus LoginStatus);

public:
	virtual ~FOnlineIdentityInterfaceEpic();

	FOnlineIdentityInterfaceEpic(FOnlineSubsystemEpic* inSubsystem);

	virtual bool Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials) override;
	virtual bool AutoLogin(int32 LocalUserNum) override;
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(const FString& Str) override;
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(uint8* Bytes, int32 Size) override;
	virtual TArray<TSharedPtr<FUserOnlineAccount>> GetAllUserAccounts() const override;
	virtual FString GetAuthToken(int32 LocalUserNum) const override;
	virtual FString GetAuthType() const override;
	virtual ELoginStatus::Type GetLoginStatus(const FUniqueNetId& UserId) const override;
	virtual ELoginStatus::Type GetLoginStatus(int32 LocalUserNum) const override;
	virtual FPlatformUserId GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const override;
	virtual FString GetPlayerNickname(const FUniqueNetId& UserId) const override;
	virtual FString GetPlayerNickname(int32 LocalUserNum) const override;
	virtual TSharedPtr<const FUniqueNetId> GetUniquePlayerId(int32 LocalUserNum) const override;
	virtual TSharedPtr<FUserOnlineAccount> GetUserAccount(const FUniqueNetId& UserId) const override;
	virtual void GetUserPrivilege(const FUniqueNetId& LocalUserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate) override;
	virtual bool Logout(int32 LocalUserNum) override;
	virtual void RevokeAuthToken(const FUniqueNetId& LocalUserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate) override;
};
