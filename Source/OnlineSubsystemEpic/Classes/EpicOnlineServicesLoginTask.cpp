#include "EpicOnlineServicesLoginTask.h"

#include "Online.h"
#include "OnlineSubsystemEpic.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSubsystemEpicTypes.h"
#include "Utilities.h"

IOnlineIdentityPtr UEpicOnlineServicesIdentityTask::GetIdentityInterface() const
{
    UWorld* World = GetWorld();
    IOnlineSubsystem* Subsystem = Online::GetSubsystem(World, EPIC_SUBSYSTEM);
    if (Subsystem == nullptr)
    {
        return nullptr;
    }
    IOnlineIdentityPtr Interface = Subsystem->GetIdentityInterface();
    if (!Interface.IsValid())
    {
        return nullptr;
    }
    return Interface;
}

// ------------------------------------
// UEpicOnlineServicesIdentityTask - Login via EOS Auth
// ------------------------------------
UEpicOnlineServicesLoginTask::UEpicOnlineServicesLoginTask(const FObjectInitializer& ObjectInitializer) :
    Super(ObjectInitializer), WorldContextObject(nullptr)
{
}

void UEpicOnlineServicesLoginTask::OnLoginCompleteDelegate(int32 LocalPlayerId, bool bWasSuccessful,
                                                           const FUniqueNetId& UserId, const FString& ErrorString)
{
    if (bWasSuccessful)
    {
        
        if(PlayerControllerWeakPtr.IsValid())
        {
            ULocalPlayer* LocalPlayer = PlayerControllerWeakPtr->GetLocalPlayer();
            if (LocalPlayer != nullptr)
            {
                LocalPlayer->SetCachedUniqueNetId(UserId.AsShared());
            }

            if (PlayerControllerWeakPtr->PlayerState != nullptr)
            {
                PlayerControllerWeakPtr->PlayerState->SetUniqueId(FUniqueNetIdRepl(UserId));
                check(PlayerControllerWeakPtr->PlayerState->GetUniqueId() == UserId);
            }
        }
        OnLoginComplete();
    }
    else
    {
        OnLoginFailed(ErrorString);
    }
}

void UEpicOnlineServicesLoginTask::OnLoginComplete()
{
    OnLoginSuccess.Broadcast(TEXT(""));
    EndTask();
}

void UEpicOnlineServicesLoginTask::OnLoginFailed(const FString& ErrorMessage)
{
    OnLoginFailure.Broadcast(ErrorMessage);
    EndTask();
}

void UEpicOnlineServicesLoginTask::EndTask()
{
    IOnlineIdentityPtr IdentityInterface = GetIdentityInterface();
    if (IdentityInterface.IsValid() && PlayerControllerWeakPtr.IsValid())
    {
        IdentityInterface->ClearOnLoginCompleteDelegate_Handle(PlayerControllerWeakPtr->NetPlayerIndex, DelegateHandle);
    }
}

void UEpicOnlineServicesLoginTask::Activate()
{
    if (IOnlineIdentityPtr IdentityInterface = this->GetIdentityInterface())
    {
        if (PlayerControllerWeakPtr.IsValid() && IdentityInterface->Login(PlayerControllerWeakPtr->NetPlayerIndex, Credentials))
        {
            // Everything went as planned, return
            return;
        }
    }

    // Something went wrong, abort
    this->OnLoginFailure.Broadcast(TEXT("Failed starting login task"));
    this->EndTask();
}

UEpicOnlineServicesLoginTask* UEpicOnlineServicesLoginTask::TryLogin(UObject* WorldContextObject, class APlayerController* PlayerController, const ELoginType LoginType, const FString UserId,
                                                                     const FString Token)
{
    UEpicOnlineServicesLoginTask* Task = NewObject<UEpicOnlineServicesLoginTask>();

    IOnlineIdentityPtr IdentityInterface = Task->GetIdentityInterface();
    if (IdentityInterface == nullptr)
    {
        Task->OnLoginFailure.Broadcast(TEXT("Failed retrieving Identity Interface"));
        Task->EndTask();
        return nullptr;
    }

    if(PlayerController == nullptr)
    {
        Task->OnLoginFailure.Broadcast(TEXT("Failed retrieving PlayerController"));
        Task->EndTask();
        return nullptr;

    }
    Task->WorldContextObject = WorldContextObject;
    Task->PlayerControllerWeakPtr = PlayerController;

    const FOnLoginCompleteDelegate LoginCompleteDelegate = FOnLoginCompleteDelegate::CreateUObject(
        Task, &UEpicOnlineServicesLoginTask::OnLoginCompleteDelegate);

    Task->DelegateHandle = IdentityInterface->AddOnLoginCompleteDelegate_Handle(
        Task->PlayerControllerWeakPtr->NetPlayerIndex, LoginCompleteDelegate);

    Task->Credentials.Type = FString::Printf(
        TEXT("EAS:%s"), *FUtils::GetEnumValueAsString<ELoginType>("ELoginType", LoginType));

    switch (LoginType)
    {
    case ELoginType::AccountPortal:
    case ELoginType::DeviceCode:
    case ELoginType::PersistentAuth:
        Task->Credentials.Id = TEXT("");
        Task->Credentials.Token = TEXT("");
        break;
    case ELoginType::ExchangeCode:
        Task->Credentials.Token = TEXT("");
        Task->Credentials.Id = UserId;
    case ELoginType::Password:
    case ELoginType::Developer:
        Task->Credentials.Id = UserId;
        Task->Credentials.Token = Token;
        break;
    default:
        break;
    }

    return Task;
}

UEpicOnlineServicesLoginTask* UEpicOnlineServicesLoginTask::TryAutoLogin(UObject* WorldContextObject, class APlayerController* PlayerController)
{
    return TryLogin(WorldContextObject, PlayerController, ELoginType::PersistentAuth, FString(), FString());
}

// ------------------------------------
// UEpicOnlineServicesConnectLoginTask - Login via EOS Connect
// ------------------------------------
UEpicOnlineServicesConnectLoginTask* UEpicOnlineServicesConnectLoginTask::TryLogin(
    int32 LocalUserNum, EConnectLoginType LoginType, FString UserId, FString Token, bool bCreateNew)
{
    UEpicOnlineServicesConnectLoginTask* Task = NewObject<UEpicOnlineServicesConnectLoginTask>();

    IOnlineIdentityPtr IdentityInterface = Task->GetIdentityInterface();
    if (IdentityInterface == nullptr)
    {
        Task->OnLoginFailure.Broadcast(TEXT("Failed retrieving Identity Interface"));
        Task->EndTask();
        return nullptr;
    }

    // Set the login delegate
    const FOnLoginCompleteDelegate LoginCompleteDelegate = FOnLoginCompleteDelegate::CreateUObject(
        Task, &UEpicOnlineServicesConnectLoginTask::OnLoginCompleteDelegate);
    Task->DelegateHandle = IdentityInterface->AddOnLoginCompleteDelegate_Handle(LocalUserNum, LoginCompleteDelegate);

    // Convert the input argument to credentials
    Task->Credentials.Id = UserId;
    Task->Credentials.Token = Token;
    Task->Credentials.Type = FString::Printf(TEXT("CONNECT:%s"), *Task->ConnectLoginTypeToString(LoginType));

    // Save if we want to create a new account if we get a continuance token
    Task->bCreateNewAccount = bCreateNew;

    return Task;
}

void UEpicOnlineServicesConnectLoginTask::Activate()
{
    IOnlineIdentityPtr IdentityInterface = this->GetIdentityInterface();
    if (IdentityInterface)
    {
        if (IdentityInterface->Login(this->LocalUserNum, this->Credentials))
        {
            // Everything went as planned, return
            return;
        }
    }

    // Something went wrong, abort
    this->OnLoginFailure.Broadcast(TEXT("Failed starting login task"));
    this->EndTask();
}

void UEpicOnlineServicesConnectLoginTask::EndTask()
{
    if (IOnlineIdentityPtr IdentityInterface = this->GetIdentityInterface())
    {
        IdentityInterface->ClearOnLoginCompleteDelegate_Handle(this->LocalUserNum, DelegateHandle);
    }
}

void UEpicOnlineServicesConnectLoginTask::OnLoginCompleteDelegate(int32 LocalPlayerId, bool bWasSuccessful,
                                                                  const FUniqueNetId& UserId,
                                                                  const FString& ErrorString)
{
    FString Error = ErrorString;
    if (bWasSuccessful)
    {
        this->OnLoginSuccess.Broadcast(TEXT(""));
    }
    else
    {
        // If the login failed, but we got an FUniqueNetId
        //  we can use continuance token to restart the process
        if (UserId.IsValid() && this->bCreateNewAccount)
        {
            UE_LOG_ONLINE_IDENTITY(Display, TEXT("Restarting login flow with continuance token."));

            IOnlineIdentityPtr identityPtr = this->GetIdentityInterface();

            FOnlineAccountCredentials contCredentials;
            contCredentials.Type = TEXT("CONNECT:Continuance");
            contCredentials.Id = TEXT("");
            contCredentials.Token = UserId.ToString();

            if (identityPtr->Login(LocalPlayerId, contCredentials))
            {
                // Everything went well, return
                return;
            }
            else
            {
                Error = TEXT("Failed restarting login flow with continuance token.");
            }
        }
        else
        {
            Error = TEXT("User doesn't exist and no new shall be created.");
        }
    }

    OnLoginFailure.Broadcast(Error);
    this->EndTask();
}

FString UEpicOnlineServicesConnectLoginTask::ConnectLoginTypeToString(const EConnectLoginType LoginType)
{
    switch (LoginType)
    {
    case EConnectLoginType::Steam:
        return TEXT("steam");
    case EConnectLoginType::PSN:
        return TEXT("psn");
    case EConnectLoginType::XBL:
        return TEXT("xbl");
    case EConnectLoginType::GOG:
        return TEXT("gog");
    case EConnectLoginType::Discord:
        return TEXT("discord");
    case EConnectLoginType::Nintendo:
        return TEXT("nintendo_id");
    case EConnectLoginType::NintendoNSA:
        return TEXT("nintendo_nsa");
    case EConnectLoginType::UPlay:
        return TEXT("uplay");
    case EConnectLoginType::OpenID:
        return TEXT("openid");
    case EConnectLoginType::DeviceId:
        return TEXT("device");
    case EConnectLoginType::Apple:
        return TEXT("apple");
    default:
        checkNoEntry();
        break;
    }

    return TEXT("");
}
