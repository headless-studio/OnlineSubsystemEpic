#include "OnlineIdentityInterfaceEpic.h"
#include "CoreMinimal.h"
#include "OnlineSubsystemEpic.h"
#include "OnlineError.h"
#include "Utilities.h"
#include "HAL/UnrealMemory.h"

#include "eos_sdk.h"
#include "eos_types.h"
#include "eos_auth.h"

//-------------------------------
// FUserOnlineAccountEpic
//-------------------------------
TSharedRef<const FUniqueNetId> FUserOnlineAccountEpic::GetUserId() const
{
    return this->UserIdPtr;
}

FString FUserOnlineAccountEpic::GetRealName() const
{
    FString realName;
    this->GetUserAttribute(USER_ATTR_REALNAME, realName);
    return realName;
}

FString FUserOnlineAccountEpic::GetDisplayName(const FString& Platform /*= FString()*/) const
{
    FString displayName;
    this->GetUserAttribute(USER_ATTR_DISPLAYNAME, displayName);
    return displayName;
}

FString FUserOnlineAccountEpic::GetAccessToken() const
{
    FString authToken;
    this->GetAuthAttribute(AUTH_ATTR_AUTHORIZATION_CODE, authToken);
    return authToken;
}

bool FUserOnlineAccountEpic::GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const
{
    const FString* FoundAttr = UserAttributes.Find(AttrName);
    if (FoundAttr != nullptr)
    {
        OutAttrValue = *FoundAttr;
        return true;
    }
    return false;
}

bool FUserOnlineAccountEpic::SetUserAttribute(const FString& AttrName, const FString& AttrValue)
{
    const FString* FoundAttr = UserAttributes.Find(AttrName);
    if (FoundAttr == nullptr || *FoundAttr != AttrValue)
    {
        UserAttributes.Add(AttrName, AttrValue);
        return true;
    }
    return false;
}

bool FUserOnlineAccountEpic::GetAuthAttribute(const FString& AttrName, FString& OutAttrValue) const
{
    // Handle special eos sdk token types
    if (AttrName == AUTH_ATTR_EA_TOKEN)
    {
        TSharedPtr<const FUniqueNetIdEpic> idPtr = StaticCastSharedPtr<
            const FUniqueNetIdEpic, const FUniqueNetId, ESPMode::NotThreadSafe>(this->UserIdPtr);
        if (idPtr)
        {
            OutAttrValue = FUniqueNetIdEpic::EpicAccountIdToString(idPtr->ToEpicAccountId());
            return true;
        }
        return false;
    }

    if (AttrName == AUTH_ATTR_ID_TOKEN)
    {
        OutAttrValue = this->UserIdPtr->ToString();
        return true;
    }

    const FString* FoundAttr = AdditionalAuthData.Find(AttrName);
    if (FoundAttr != nullptr)
    {
        OutAttrValue = *FoundAttr;
        return true;
    }
    return false;
}

bool FUserOnlineAccountEpic::SetAuthAttribute(const FString& AttrName, const FString& AttrValue)
{
    this->AdditionalAuthData.Add(AttrName, AttrValue);
    return true;
}


// ---------------------------------------------
// Implementation file only structs
// These structs carry additional informations to the callbacks
// ---------------------------------------------
typedef struct FLoginCompleteAdditionalData
{
    FOnlineIdentityInterfaceEpic* IdentityInterface;
    int32 LocalUserNum;
    EOS_EpicAccountId EpicAccountId;
} FAuthLoginCompleteAdditionalData;

typedef struct FCreateUserAdditionalData
{
    FOnlineIdentityInterfaceEpic* IdentityInterface;
    int32 LocalUserNum;
} FCreateUserAdditionalData;

// -----------------------------
// EOS Callbacks
// -----------------------------
void EOS_CALL FOnlineIdentityInterfaceEpic::EOS_Auth_OnLoginComplete(const EOS_Auth_LoginCallbackInfo* Data)
{
    check(Data != nullptr);
    // To raise the login complete delegates the interface itself has to be retrieved from the returned data
    FLoginCompleteAdditionalData* AdditionalData = static_cast<FLoginCompleteAdditionalData*>(Data->ClientData);

    FOnlineIdentityInterfaceEpic* InterfaceEpic = AdditionalData->IdentityInterface;
    check(InterfaceEpic != nullptr);

    FString ErrorMessage;

    if (Data->ResultCode == EOS_EResult::EOS_Success)
    {
        const EOS_EpicAccountId AccountId = EOS_Auth_GetLoggedInAccountByIndex(InterfaceEpic->AuthHandle, AdditionalData->LocalUserNum);
        if (EOS_EpicAccountId_IsValid(AccountId))
        {
            EOS_Auth_Token* AuthToken = nullptr;

            EOS_Auth_CopyUserAuthTokenOptions CopyAuthTokenOptions = {
                EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST
            };
            EOS_Auth_CopyUserAuthToken(InterfaceEpic->AuthHandle, &CopyAuthTokenOptions, AccountId, &AuthToken);

            EOS_Connect_Credentials ConnectCredentials = {
                EOS_CONNECT_CREDENTIALS_API_LATEST,
                AuthToken->AccessToken,
                EOS_EExternalCredentialType::EOS_ECT_EPIC
            };
            EOS_Connect_LoginOptions LoginOptions = {
                EOS_CONNECT_LOGIN_API_LATEST,
                &ConnectCredentials,
                nullptr
            };

            FLoginCompleteAdditionalData* NewAdditionalData = new FLoginCompleteAdditionalData{
                InterfaceEpic,
                AdditionalData->LocalUserNum,
                AccountId
            };
            EOS_Connect_Login(InterfaceEpic->ConnectHandle, &LoginOptions, NewAdditionalData, EOS_Connect_OnLoginComplete);

            // Release the auth token
            EOS_Auth_Token_Release(AuthToken);
        }
        else
        {
            ErrorMessage = TEXT("[EOS SDK] Invalid epic user id");
        }
    }
    else
    {
        ErrorMessage = FString::Printf(
            TEXT("[EOS SDK] Auth Login Failed - Error Code: %s"),
            UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
    }

    // Abort if there was an error
    if (!ErrorMessage.IsEmpty())
    {
        UE_LOG_ONLINE_IDENTITY(Warning, TEXT("Epic Account Service Login failed. Message:\r\n    %s"), *ErrorMessage);
        InterfaceEpic->TriggerOnLoginCompleteDelegates(INDEX_NONE, false, FUniqueNetIdEpic(), ErrorMessage);
    }

    delete(AdditionalData);
}

void EOS_CALL FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginComplete(const EOS_Connect_LoginCallbackInfo* Data)
{
    FLoginCompleteAdditionalData* AdditionalData = static_cast<FLoginCompleteAdditionalData*>(Data->ClientData);

    FOnlineIdentityInterfaceEpic* InterfaceEpic = AdditionalData->IdentityInterface;
    check(InterfaceEpic);


    FString ErrorMessage;

    FUniqueNetIdEpic UserId;
    const EOS_EResult Result = Data->ResultCode;
    if (Result == EOS_EResult::EOS_Success)
    {
        if (Data->ContinuanceToken)
        {
            // ToDo: Implementing linking user accounts needs some changes
            // to the FUniqueNetIdEpic type
        }

        UserId = FUniqueNetIdEpic(Data->LocalUserId, AdditionalData->EpicAccountId);
        //Added a pretty print for the user ID here as the log before was spitting undefined characters - Mike
        UE_LOG_ONLINE_IDENTITY(Display, TEXT("Finished logging in user \"%s\""), *UserId.ToDebugString());
    }
    else if (Result == EOS_EResult::EOS_InvalidUser)
    {
        if (Data->ContinuanceToken)
        {
            // Getting a continuance token implies the login has failed, however we want to give the caller
            // the ability to restart the login with the continuance token.
            // Thus we create an FUniqueNetIdString from the token and return it to the caller with a failed
            // login indication.
            UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Got invalid user and contiuance token."));
            const FUniqueNetIdString ContinuanceToken = FUniqueNetIdString(UTF8_TO_TCHAR(Data->ContinuanceToken));
            InterfaceEpic->TriggerOnLoginCompleteDelegates(AdditionalData->LocalUserNum, false, ContinuanceToken, TEXT(""));
        }
        else
        {
            ErrorMessage = TEXT("[EOS SDK] Got invalid user, but no continuance token.");
        }
    }
    else
    {
        ErrorMessage = FString::Printf(
            TEXT("[EOS SDK] Connect Login Failed - Error Code: %s"),
            UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
    }

    if (!ErrorMessage.IsEmpty())
    {
        UE_LOG_ONLINE_IDENTITY(Warning, TEXT("%s encountered an error. Message:\r\n    %s"), *FString(__FUNCTION__),
                               *ErrorMessage);
        InterfaceEpic->TriggerOnLoginCompleteDelegates(AdditionalData->LocalUserNum, false, FUniqueNetIdEpic(), ErrorMessage);
    }
    else
    {
        InterfaceEpic->TriggerOnLoginCompleteDelegates(AdditionalData->LocalUserNum, true, UserId, TEXT(""));
    }

    delete(AdditionalData);
}

void EOS_CALL FOnlineIdentityInterfaceEpic::EOS_Connect_OnAuthExpiration(EOS_Connect_AuthExpirationCallbackInfo const* Data)
{
    // ToDo: Make the user see this.
    FString localUser = FUniqueNetIdEpic::ProductUserIdToString(Data->LocalUserId);
    UE_LOG_ONLINE_IDENTITY(Display, TEXT("Auth for user \"%s\" expired"), *localUser);
}

void EOS_CALL FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginStatusChanged(
    EOS_Connect_LoginStatusChangedCallbackInfo const* Data)
{
    FOnlineIdentityInterfaceEpic* thisPtr = (FOnlineIdentityInterfaceEpic*)Data->ClientData;

    FString localUser = FUniqueNetIdEpic::ProductUserIdToString(Data->LocalUserId);
    ELoginStatus::Type oldStatus = thisPtr->EOSLoginStatusToUELoginStatus(Data->PreviousStatus);
    ELoginStatus::Type newStatus = thisPtr->EOSLoginStatusToUELoginStatus(Data->CurrentStatus);


    // ToDo: Somehow this always crashes
    //UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Login status changed.\r\n%9s: %s\r\n%9s: %s\r\n%9s: %s"), TEXT("User"), *localUser, TEXT("New State"), *ELoginStatus::ToString(newStatus), TEXT("Old State"), *ELoginStatus::ToString(oldStatus));

    FUniqueNetIdEpic netId = FUniqueNetIdEpic(Data->LocalUserId);
    FPlatformUserId localUserNum = thisPtr->GetPlatformUserIdFromUniqueNetId(netId);

    thisPtr->TriggerOnLoginStatusChangedDelegates(localUserNum, oldStatus, newStatus, netId);
}

void EOS_CALL FOnlineIdentityInterfaceEpic::EOS_Auth_OnLogoutComplete(const EOS_Auth_LogoutCallbackInfo* Data)
{
    if (Data->ResultCode != EOS_EResult::EOS_Success)
    {
        char const* resultStr = EOS_EResult_ToString(Data->ResultCode);
        UE_LOG_ONLINE_IDENTITY(Warning, TEXT("[EOS SDK] Logout Failed - User: %s, Result : %s"),
                               UTF8_TO_TCHAR(Data->LocalUserId), resultStr);
        return;
    }

    FOnlineIdentityInterfaceEpic* thisPtr = (FOnlineIdentityInterfaceEpic*)Data->ClientData;
    check(thisPtr);

    EOS_ProductUserId puid = FUniqueNetIdEpic::ProductUserIDFromString(UTF8_TO_TCHAR(Data->LocalUserId));
    int32 idIdx = thisPtr->GetPlatformUserIdFromUniqueNetId(FUniqueNetIdEpic(puid));

    thisPtr->TriggerOnLogoutCompleteDelegates(idIdx, true);
    FString localUser = FUniqueNetIdEpic::EpicAccountIdToString(Data->LocalUserId);
    UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logout Complete - User: %s"), *localUser);
}

void EOS_CALL FOnlineIdentityInterfaceEpic::EOS_Connect_OnUserCreated(EOS_Connect_CreateUserCallbackInfo const* Data)
{
    FCreateUserAdditionalData* additionalData = static_cast<FCreateUserAdditionalData*>(Data->ClientData);
    FOnlineIdentityInterfaceEpic* thisPtr = additionalData->IdentityInterface;
    check(thisPtr);

    if (Data->ResultCode != EOS_EResult::EOS_Success)
    {
        char const* resultStr = EOS_EResult_ToString(Data->ResultCode);
        FString error = FString::Printf(
            TEXT("[EOS SDK] Create User Failed - Result : %s"), UTF8_TO_TCHAR(Data->LocalUserId), resultStr);
        thisPtr->TriggerOnLoginCompleteDelegates(additionalData->LocalUserNum, false, FUniqueNetIdEpic(), error);
        return;
    }

    // Creating a user always means we're not using an epic account
    FUniqueNetIdEpic userId = FUniqueNetIdEpic(Data->LocalUserId);
    UE_LOG_ONLINE_IDENTITY(Display, TEXT("Finished creating user \"%s\""), UTF8_TO_TCHAR(Data->LocalUserId));

    thisPtr->TriggerOnLoginCompleteDelegates(additionalData->LocalUserNum, true, userId, TEXT(""));
}

void EOS_CALL FOnlineIdentityInterfaceEpic::EOS_Connect_OnAccountLinked(EOS_Connect_LinkAccountCallbackInfo const* Data)
{
    // ToDo: Implement a way to notify the user that an account was linked
}

//-------------------------------
// FOnlineIdentityInterfaceEpic
//-------------------------------
FOnlineIdentityInterfaceEpic::FOnlineIdentityInterfaceEpic(FOnlineSubsystemEpic* inSubsystem)
    : SubsystemEpic(inSubsystem)
{
    this->AuthHandle = EOS_Platform_GetAuthInterface(inSubsystem->PlatformHandle);
    this->ConnectHandle = EOS_Platform_GetConnectInterface(inSubsystem->PlatformHandle);

    EOS_Connect_AddNotifyAuthExpirationOptions expirationOptions = {
        EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST
    };
    this->NotifyAuthExpiration = EOS_Connect_AddNotifyAuthExpiration(this->ConnectHandle, &expirationOptions, this,
                                                                     &FOnlineIdentityInterfaceEpic::EOS_Connect_OnAuthExpiration);

    EOS_Connect_AddNotifyLoginStatusChangedOptions loginStatusChangedOptions = {
        EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST
    };
    this->NotifyLoginStatusChangedId = EOS_Connect_AddNotifyLoginStatusChanged(
        this->ConnectHandle, &loginStatusChangedOptions, this,
        &FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginStatusChanged);
}

FOnlineIdentityInterfaceEpic::~FOnlineIdentityInterfaceEpic()
{
    EOS_Connect_RemoveNotifyLoginStatusChanged(this->ConnectHandle, this->NotifyLoginStatusChangedId);
    EOS_Connect_RemoveNotifyAuthExpiration(this->ConnectHandle, this->NotifyAuthExpiration);
}

bool FOnlineIdentityInterfaceEpic::Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials)
{
    // The account credentials struct has the following format
    // The "Type" field is a string that encodes the login system and login type for that system.
    // Both parts are separated by a single ":" character. The login system must either be "EAS" or "CONNECT",
    // while the login type is a string representation of "ELoginType" for "EAS"
    // and "EOS_EExternalCredentialType" when using "CONNECT"
    // The "Id" and "Token" fields encode different values depending on the login type set. 
    // If a value in the table is marked as "N/A", no values besides the "Type" field need to be set
    // Note: In the future this might be simplified by a helper class that allows the  fields to be set and offer validation
    // | System    | Type                     | Mapping                                         |
    // |-------- - | ------------------------ | ------------------------------------------------|
    // | EAS       | Password                 | Username -> Id, Password -> Token               |
    // |           | Exchange Code            | Exchange Code -> Id                             |
    // |           | Device Code              | N / A                                           |
    // |           | Developer                | Account Id -> Id                                |
    // |           | Account Portal           | N / A                                           |
    // |           | Persistent Auth          | N / A                                           |
    // |           | External Auth            | External System -> Id, External Token -> Token  |
    // | CONNECT   | Apple, Nintendo (NSA)    | Display Name -> Id                              |
    // |           | Other                    | N / A                                           |


    FString ErrorMessage;
    bool bSuccess = false;

    if (0 <= LocalUserNum && LocalUserNum < MAX_LOCAL_PLAYERS)
    {
        // Check if we are using the epic account system or plain connect
        FString Left, Right;
        AccountCredentials.Type.Split(TEXT(":"), &Left, &Right);
        if (Left.Equals(TEXT("EAS"), ESearchCase::IgnoreCase))
        {
            EOS_EpicAccountId eosId = EOS_Auth_GetLoggedInAccountByIndex(this->AuthHandle, LocalUserNum);

            // If we already have a valid EAID, we can go straight to connect
            if (EOS_EpicAccountId_IsValid(eosId))
            {
                EOS_Auth_Token* AuthToken = nullptr;

                EOS_Auth_CopyUserAuthTokenOptions CopyAuthTokenOptions = {
                    EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST
                };
                EOS_Auth_CopyUserAuthToken(this->AuthHandle, &CopyAuthTokenOptions, eosId, &AuthToken);

                EOS_Connect_Credentials ConnectCredentials = {
                    EOS_CONNECT_CREDENTIALS_API_LATEST,
                    AuthToken->AccessToken,
                    EOS_EExternalCredentialType::EOS_ECT_EPIC
                };
                EOS_Connect_LoginOptions LoginOptions = {
                    EOS_CONNECT_LOGIN_API_LATEST,
                    &ConnectCredentials,
                    nullptr
                };
                FLoginCompleteAdditionalData* AdditionalData = new FLoginCompleteAdditionalData{
                    this,
                    LocalUserNum,
                    nullptr
                };
                EOS_Connect_Login(ConnectHandle, &LoginOptions, AdditionalData, EOS_Connect_OnLoginComplete);

                // Release the auth token
                EOS_Auth_Token_Release(AuthToken);
            }
                // In any other case we call the epic account endpoint
                // and handle login in the callback
            else
            {
                // Create credentials struct
                EOS_Auth_Credentials Credentials = {
                    EOS_AUTH_CREDENTIALS_API_LATEST
                };

                // Get which EAS login type we want to use
                ELoginType loginType = FUtils::GetEnumValueFromString<ELoginType>(TEXT("ELoginType"), Right);

                // Convert the Id and Token fields to char const* for later use
                char const* FirstParamStr = TCHAR_TO_ANSI(*AccountCredentials.Id.Left(256));
                char const* SecondParamStr = TCHAR_TO_ANSI(*AccountCredentials.Token.Left(256));

                switch (loginType)
                {
                case ELoginType::Password:
                    {
                        UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In as User Id: %hs"), FirstParamStr);
                        Credentials.Id = FirstParamStr;
                        Credentials.Token = SecondParamStr;
                        Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_Password;
                        break;
                    }
                case ELoginType::ExchangeCode:
                    {
                        UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Exchange Code"));
                        Credentials.Token = FirstParamStr;
                        Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
                        break;
                    }
                case ELoginType::DeviceCode:
                    {
                        UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Device Code"));
                        Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_DeviceCode;
                        break;
                    }
                case ELoginType::Developer:
                    {
                        UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Host: %s"),
                                               *this->SubsystemEpic->DevToolAddress);
                        Credentials.Id = TCHAR_TO_ANSI(*this->SubsystemEpic->DevToolAddress);
                        Credentials.Token = FirstParamStr;
                        Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_Developer;
                        break;
                    }
                case ELoginType::AccountPortal:
                    {
                        UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Account Portal"));
                        Credentials.Id = FirstParamStr;
                        Credentials.Token = SecondParamStr;
                        Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
                        break;
                    }
                case ELoginType::PersistentAuth:
                    {
                        UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] Logging In with Persistent Auth"));
                        Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
                        break;
                    }
                case ELoginType::ExternalAuth:
                    {
                        // Set ther type to external and copy the token. These are always the same
                        Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExternalAuth;
                        Credentials.Token = SecondParamStr;

                        // Check which external login provider we want to use and then set the external type to the appropriate value
                        EExternalLoginType externalLoginType = FUtils::GetEnumValueFromString<EExternalLoginType>(
                            TEXT("EExternalLoginType"), AccountCredentials.Id);
                        switch (externalLoginType)
                        {
                        case EExternalLoginType::Steam:
                            {
                                Credentials.ExternalType = EOS_EExternalCredentialType::EOS_ECT_STEAM_APP_TICKET;
                                break;
                            }
                        default:
                            ErrorMessage = FString::Printf(
                                TEXT("Using unsupported external login type: %s"), *AccountCredentials.Id);
                            break;
                        }
                        break;
                    }
                default:
                    {
                        UE_LOG_ONLINE_IDENTITY(Fatal, TEXT("Login of type \"%s\" not supported"),
                                               *AccountCredentials.Type);
                        return false;
                    }
                }

                EOS_Auth_LoginOptions LoginOptions = {};
                LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
                LoginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile |
                    EOS_EAuthScopeFlags::EOS_AS_FriendsList | EOS_EAuthScopeFlags::EOS_AS_Presence;
                LoginOptions.Credentials = &Credentials;

                FLoginCompleteAdditionalData* AdditionalData = new FLoginCompleteAdditionalData{
                    this,
                    LocalUserNum
                };
                EOS_Auth_Login(AuthHandle, &LoginOptions, AdditionalData, EOS_Auth_OnLoginComplete);
            }
            bSuccess = true;
        }
        else if (Left.Equals(TEXT("CONNECT"), ESearchCase::IgnoreCase))
        {
            if (Right.Equals(TEXT("Continuance"), ESearchCase::IgnoreCase))
            {
                unimplemented();

                //EOS_Connect_CreateUserOptions createUserOptions = {
                //	EOS_CONNECT_CREATEUSER_API_LATEST,
                //	TCHAR_TO_UTF8(*AccountCredentials.Token)
                //};
                //FCreateUserAdditionalData* additionalData = new FCreateUserAdditionalData{
                //	this,
                //	LocalUserNum
                //};
                //EOS_Connect_CreateUser(this->connectHandle, &createUserOptions, this, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnUserCreated);
            }
            else if (Right.Equals(TEXT("Link"), ESearchCase::IgnoreCase))
            {
                unimplemented();
                //EOS_ProductUserId puid = FUniqueNetIdEpic::ProductUserIDFromString(AccountCredentials.Id);

                //EOS_Connect_LinkAccountOptions linkAccountOptions = {
                //	EOS_CONNECT_LINKACCOUNT_API_LATEST,
                //	puid,
                //	TCHAR_TO_UTF8(*AccountCredentials.Token)
                //};
                //FCreateUserAdditionalData* additionalData = new FCreateUserAdditionalData{
                //	this,
                //	LocalUserNum
                //};
                //EOS_Connect_LinkAccount(this->connectHandle, &linkAccountOptions, additionalData, &FOnlineIdentityInterfaceEpic::EOS_Connect_OnAccountLinked);
            }
            else
            {
                // Convert the right part of the type string into a EOS external credentials enum.
                TPair<EOS_EExternalCredentialType, bool> externalTypeTuple =
                    FUtils::ExternalCredentialsTypeFromString(Right);
                EOS_EExternalCredentialType externalType = externalTypeTuple.Get<0>();

                // Make sure we have a valid connect type
                if (externalTypeTuple.Get<1>())
                {
                    EOS_Connect_Credentials connectCrendentials = {
                        EOS_CONNECT_CREDENTIALS_API_LATEST,
                        TCHAR_TO_UTF8(*AccountCredentials.Token),
                        externalType
                    };

                    // We need to check which external credentials type is used,
                    // as Apple and Nintendo require additional data.
                    EOS_Connect_LoginOptions loginOptions;
                    if (externalType == EOS_EExternalCredentialType::EOS_ECT_APPLE_ID_TOKEN
                        || externalType == EOS_EExternalCredentialType::EOS_ECT_NINTENDO_ID_TOKEN
                        || externalType == EOS_EExternalCredentialType::EOS_ECT_NINTENDO_NSA_ID_TOKEN)
                    {
                        EOS_Connect_UserLoginInfo loginInfo = {
                            EOS_CONNECT_USERLOGININFO_API_LATEST,
                            TCHAR_TO_UTF8(*AccountCredentials.Id)
                        };
                        loginOptions = {
                            EOS_CONNECT_LOGIN_API_LATEST,
                            &connectCrendentials,
                            &loginInfo
                        };
                    }
                    else
                    {
                        loginOptions = {
                            EOS_CONNECT_LOGIN_API_LATEST,
                            &connectCrendentials,
                            nullptr
                        };
                    }

                    FLoginCompleteAdditionalData* additionalData = new FLoginCompleteAdditionalData{
                        this,
                        LocalUserNum,
                        nullptr // Since this is the connect login flow, no EAID is available
                    };
                    EOS_Connect_Login(this->ConnectHandle, &loginOptions, additionalData,
                                      &FOnlineIdentityInterfaceEpic::EOS_Connect_OnLoginComplete);

                    bSuccess = true;
                }
                else
                {
                    ErrorMessage = FString::Printf(TEXT("\"%s\" is not a recognized connect type"), *Right);
                }
            }
        }
        else if (Left.IsEmpty() || Right.IsEmpty())
        {
            ErrorMessage = TEXT("Must specify login flow.");
        }
        else
        {
            ErrorMessage = FString::Printf(TEXT("\"%s\" is not a recognized login flow."), *Left);
        }
    }
    else
    {
        ErrorMessage = FString::Printf(
            TEXT("\"%d\" is outside the range of allowed user indices [0 - %d["), LocalUserNum, MAX_LOCAL_PLAYERS);
    }

    if (!bSuccess)
    {
        UE_CLOG_ONLINE_IDENTITY(!ErrorMessage.IsEmpty(), Warning, TEXT("%s encountered an error. Message:\r\n    %s"),
                                *FString(__FUNCTION__), *ErrorMessage);
        this->TriggerOnLoginCompleteDelegates(LocalUserNum, false, FUniqueNetIdEpic(), ErrorMessage);
    }

    return bSuccess;
}

bool FOnlineIdentityInterfaceEpic::AutoLogin(int32 LocalUserNum)
{
    FOnlineAccountCredentials Credentials;
    Credentials.Type = FString("EAS:PersistentAuth");

    return Login(LocalUserNum, Credentials);
}

TSharedPtr<const FUniqueNetId> FOnlineIdentityInterfaceEpic::CreateUniquePlayerId(const FString& Str)
{
    // This might not be useful, but we only create a new PUID from this
    return MakeShared<FUniqueNetIdEpic>(FUniqueNetIdEpic::ProductUserIDFromString(Str));
}

TSharedPtr<const FUniqueNetId> FOnlineIdentityInterfaceEpic::CreateUniquePlayerId(uint8* Bytes, int32 Size)
{
    if (Bytes && Size > 0)
    {
        const FString StrId(Size, reinterpret_cast<TCHAR*>(Bytes));
        return CreateUniquePlayerId(StrId);
    }
    return nullptr;
}

TArray<TSharedPtr<FUserOnlineAccount>> FOnlineIdentityInterfaceEpic::GetAllUserAccounts() const
{
    TArray<TSharedPtr<FUserOnlineAccount>> Accounts;

    const int32 LoggedInCount = EOS_Connect_GetLoggedInUsersCount(ConnectHandle);
    for (int32 i = 0; i < LoggedInCount; ++i)
    {
        EOS_ProductUserId ProductUserId = EOS_Connect_GetLoggedInUserByIndex(ConnectHandle, i);

        TSharedPtr<FUserOnlineAccount> UserAccount = OnlineUserAccountFromPUID(ProductUserId);

        Accounts.Add(UserAccount);
    }
    return Accounts;
}

TSharedPtr<FUserOnlineAccount> FOnlineIdentityInterfaceEpic::GetUserAccount(const FUniqueNetId& UserId) const
{
    const TSharedRef<FUniqueNetIdEpic const> EpicNetId = StaticCastSharedRef<FUniqueNetIdEpic const>(UserId.AsShared());
    const EOS_ProductUserId ProductUserId = EpicNetId->ToProductUserId();

    return OnlineUserAccountFromPUID(ProductUserId);
}

FString FOnlineIdentityInterfaceEpic::GetAuthToken(const int32 LocalUserNum) const
{
    const TSharedPtr<const FUniqueNetId> UniqueNetId = this->GetUniquePlayerId(LocalUserNum);
    if (UniqueNetId.IsValid())
    {
        const TSharedPtr<FUserOnlineAccount> OnlineAccount = this->GetUserAccount(*UniqueNetId);
        if (OnlineAccount.IsValid())
        {
            return OnlineAccount->GetAccessToken();
        }
    }
    return FString();
}

FString FOnlineIdentityInterfaceEpic::GetAuthType() const
{
    return EPIC_SUBSYSTEM.ToString();
}

ELoginStatus::Type FOnlineIdentityInterfaceEpic::GetLoginStatus(const FUniqueNetId& UserId) const
{
    const FUniqueNetIdEpic EpicUserId = static_cast<FUniqueNetIdEpic>(UserId);
    if (EpicUserId.IsProductUserIdValid())
    {
        const EOS_ELoginStatus LoginStatus = EOS_Connect_GetLoginStatus(this->ConnectHandle, EpicUserId.ToProductUserId());
        switch (LoginStatus)
        {
        case EOS_ELoginStatus::EOS_LS_NotLoggedIn:
            return ELoginStatus::NotLoggedIn;
        case EOS_ELoginStatus::EOS_LS_UsingLocalProfile:
            return ELoginStatus::UsingLocalProfile;
        case EOS_ELoginStatus::EOS_LS_LoggedIn:
            return ELoginStatus::LoggedIn;
        default:
            checkNoEntry();
        }
    }
    return ELoginStatus::NotLoggedIn;
}

ELoginStatus::Type FOnlineIdentityInterfaceEpic::GetLoginStatus(const int32 LocalUserNum) const
{
    const TSharedPtr<const FUniqueNetId> UniqueNetId = GetUniquePlayerId(LocalUserNum);
    if (UniqueNetId.IsValid())
    {
        return GetLoginStatus(*UniqueNetId);
    }
    return ELoginStatus::NotLoggedIn;
}

FPlatformUserId FOnlineIdentityInterfaceEpic::GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const
{
    for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i)
    {
        auto id = GetUniquePlayerId(i);
        if (id.IsValid() && (*id == UniqueNetId))
        {
            return i;
        }
    }

    return PLATFORMUSERID_NONE;
}

FString FOnlineIdentityInterfaceEpic::GetPlayerNickname(const FUniqueNetId& UserId) const
{
    const TSharedPtr<FUserOnlineAccount> OnlineAccount = this->GetUserAccount(UserId);
    if (!OnlineAccount.IsValid())
    {
        return TEXT("");
    }
    return OnlineAccount->GetDisplayName();
}

FString FOnlineIdentityInterfaceEpic::GetPlayerNickname(int32 LocalUserNum) const
{
    TSharedPtr<const FUniqueNetId> id = GetUniquePlayerId(LocalUserNum);
    if (!id.IsValid())
    {
        return TEXT("");
    }
    return this->GetPlayerNickname(*id);
}

TSharedPtr<const FUniqueNetId> FOnlineIdentityInterfaceEpic::GetUniquePlayerId(int32 LocalUserNum) const
{
    EOS_ProductUserId AccountId = EOS_Connect_GetLoggedInUserByIndex(this->ConnectHandle, LocalUserNum);
    EOS_EpicAccountId EpicAccountId = EOS_Auth_GetLoggedInAccountByIndex(this->AuthHandle, LocalUserNum);

    if (EOS_ProductUserId_IsValid(AccountId))
    {
        // We don't care if the EAID is invalid
        return MakeShared<FUniqueNetIdEpic>(AccountId, EpicAccountId);
    }
    return nullptr;
}

void FOnlineIdentityInterfaceEpic::GetUserPrivilege(const FUniqueNetId& LocalUserId, EUserPrivileges::Type Privilege,
                                                    const FOnGetUserPrivilegeCompleteDelegate& Delegate)
{
    // ToDo: Implement actual check against the backend
    Delegate.ExecuteIfBound(LocalUserId, Privilege, static_cast<uint32>(EPrivilegeResults::NoFailures));
}

bool FOnlineIdentityInterfaceEpic::Logout(int32 LocalUserNum)
{
    FString Error;

    const TSharedPtr<const FUniqueNetIdEpic> NetIdEpic = StaticCastSharedPtr<const FUniqueNetIdEpic>(
        this->GetUniquePlayerId(LocalUserNum));
    if (NetIdEpic != nullptr)
    {
        if (NetIdEpic->IsEpicAccountIdValid())
        {
            EOS_Auth_LogoutOptions LogoutOpts = {
                EOS_AUTH_LOGOUT_API_LATEST,
                NetIdEpic->ToEpicAccountId()
            };

            const EOS_EpicAccountId EpicAccountId = EOS_Auth_GetLoggedInAccountByIndex(AuthHandle, LocalUserNum);
            LogoutOpts.LocalUserId = EpicAccountId;
            EOS_Auth_Logout(AuthHandle, &LogoutOpts, this, &FOnlineIdentityInterfaceEpic::EOS_Auth_OnLogoutComplete);
        }
        else
        {
            UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] No valid epic account id for logout found."));
        }

        // Remove the user id from the local cache
        TSharedRef<const FUniqueNetIdEpic> idRef = NetIdEpic.ToSharedRef();
        ///this->userAccounts.Remove(idRef);
    }
    else
    {
        Error = FString::Printf(
            TEXT("%s: No valid user id found for local user %d."), *FString(__FUNCTION__), LocalUserNum);
    }

    if (!Error.IsEmpty())
    {
        UE_LOG_ONLINE_IDENTITY(Warning, TEXT("%s encountered an error. Message:\r\n    %s"), *FString(__FUNCTION__),
                               *Error);
        TriggerOnLogoutCompleteDelegates(LocalUserNum, false);
    }

    return Error.IsEmpty();
}

void FOnlineIdentityInterfaceEpic::RevokeAuthToken(const FUniqueNetId& LocalUserId,
                                                   const FOnRevokeAuthTokenCompleteDelegate& Delegate)
{
    UE_LOG_ONLINE_IDENTITY(Fatal, TEXT("FOnlineIdentityInterfaceEpic::RevokeAuthToken not implemented"));
}


//-------------------------------
// Utility Methods
//-------------------------------

TSharedPtr<FUserOnlineAccount> FOnlineIdentityInterfaceEpic::OnlineUserAccountFromPUID(
    EOS_ProductUserId const& PUID) const
{
    EOS_Connect_ExternalAccountInfo* ExternalAccountInfo = nullptr;

    EOS_Connect_CopyProductUserInfoOptions CopyUserInfoOptions = {
        EOS_CONNECT_COPYPRODUCTUSERINFO_API_LATEST,
        PUID
    };

    const EOS_EResult ConnectResult = EOS_Connect_CopyProductUserInfo(ConnectHandle, &CopyUserInfoOptions, &ExternalAccountInfo);
    const FString ConnectResultStr = FString(EOS_EResult_ToString(ConnectResult));
    UE_LOG(LogTemp, Display, TEXT("Result for Copy %s"), *ConnectResultStr);

    TSharedPtr<FUserOnlineAccountEpic> UserAccount = nullptr;

    check(ExternalAccountInfo != nullptr);
    
    // Check if this user account is also owned by EPIC and if, make calls to the Auth interface
    if (ExternalAccountInfo->AccountIdType == EOS_EExternalAccountType::EOS_EAT_EPIC)
    {
        EOS_EpicAccountId EpicAccountId = EOS_EpicAccountId_FromString(ExternalAccountInfo->AccountId);
        if (EOS_EpicAccountId_IsValid(EpicAccountId))
        {
            EOS_Auth_Token* AuthToken = nullptr;

            EOS_Auth_CopyUserAuthTokenOptions copyUserAuthTokenOptions = {
                EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST
            };
            const EOS_EResult EosResult = EOS_Auth_CopyUserAuthToken(this->AuthHandle, &copyUserAuthTokenOptions, EpicAccountId,
                                                                     &AuthToken);
            if (EosResult == EOS_EResult::EOS_Success)
            {
                TSharedRef<FUniqueNetId> UniqueNetId = MakeShared<FUniqueNetIdEpic>(PUID, EpicAccountId);
                UserAccount = MakeShared<FUserOnlineAccountEpic>(UniqueNetId);

                UserAccount->SetAuthAttribute(AUTH_ATTR_REFRESH_TOKEN, UTF8_TO_TCHAR(AuthToken->RefreshToken));
                // ToDo: Discussion needs to happen, what else to include into the UserOnlineAccount
            }
            else
            {
                UE_LOG_ONLINE_IDENTITY(Display, TEXT("[EOS SDK] No auth token for EAID %s"),
                                       UTF8_TO_TCHAR(ExternalAccountInfo->AccountId));
            }

            EOS_Auth_Token_Release(AuthToken);
        }
        else
        {
            UE_LOG_ONLINE_IDENTITY(Display, TEXT("External account id was epic, but account id is invalid"));
        }
    }

    // Check if we already created a user account with EPIC data
    if (!UserAccount)
    {
        TSharedRef<FUniqueNetId> UniqueNetId = MakeShared<FUniqueNetIdEpic>(PUID);
        UserAccount = MakeShared<FUserOnlineAccountEpic>(UniqueNetId);
    }

    UserAccount->SetUserAttribute(USER_ATTR_DISPLAYNAME, UTF8_TO_TCHAR(ExternalAccountInfo->DisplayName));
    UserAccount->SetUserAttribute(
        USER_ATTR_LAST_LOGIN_TIME, FString::Printf(TEXT("%lld"), ExternalAccountInfo->LastLoginTime));

    EOS_Connect_ExternalAccountInfo_Release(ExternalAccountInfo);

    return UserAccount;
}

ELoginStatus::Type FOnlineIdentityInterfaceEpic::EOSLoginStatusToUELoginStatus(EOS_ELoginStatus LoginStatus)
{
    switch (LoginStatus)
    {
    case EOS_ELoginStatus::EOS_LS_NotLoggedIn:
        return ELoginStatus::NotLoggedIn;
    case EOS_ELoginStatus::EOS_LS_UsingLocalProfile:
        return ELoginStatus::UsingLocalProfile;
    case EOS_ELoginStatus::EOS_LS_LoggedIn:
        return ELoginStatus::LoggedIn;
    default:
        checkNoEntry();
    }

    return ELoginStatus::NotLoggedIn;
}
