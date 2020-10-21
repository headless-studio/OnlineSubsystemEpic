#pragma once

#include "UObject/Object.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "OnlineSubsystemEpicTypes.h"

#include "EpicOnlineServicesLoginTask.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoginResultPin, const FString&, ErrorMessage);

// DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoginRequresMFAPin);

UCLASS(MinimalAPI)
class UEpicOnlineServicesIdentityTask : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    IOnlineIdentityPtr GetIdentityInterface() const;
protected:
    FDelegateHandle DelegateHandle;
};


/**
 * Exposes the epic account login flow to blueprints
 */
UCLASS(BlueprintType, meta = (ExposedAsyncProxy = AsyncTask))
class ONLINESUBSYSTEMEPIC_API UEpicOnlineServicesLoginTask : public UEpicOnlineServicesIdentityTask
{
    GENERATED_UCLASS_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
    FOnLoginResultPin OnLoginSuccess;

    // UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
    // FOnLoginFailurePin OnLoginRequiresMFA;

    // Login failed.
    UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
    FOnLoginResultPin OnLoginFailure;

    /**
     * Attempts to log in with the specified epic account login flow and credentials
     */
    UFUNCTION(BlueprintCallable, Category = "Epic Online Services|Identity", meta = (BlueprintInternalUseOnly = "true", WorldContext="WorldContextObject",
        DisplayName = "Epic Online Subsystem Login"))
    static UEpicOnlineServicesLoginTask* TryLogin(UObject* WorldContextObject, class APlayerController* PlayerController, const ELoginType LoginType, const FString UserId, const FString Token);

    // /**
    //  * Attempts to log in with locally stored credentials
    //  */
    UFUNCTION(BlueprintCallable, Category = "Epic Online Services|Identity", meta = (BlueprintInternalUseOnly = "true", WorldContext="WorldContextObject",
        DisplayName = "Epic Online Subsystem Auto Login"))
    static UEpicOnlineServicesLoginTask* TryAutoLogin(UObject* WorldContextObject, class APlayerController* PlayerController);

    /** UBlueprintAsyncActionBase interface */
    virtual void Activate() override;

private:
    FOnlineAccountCredentials Credentials;
    TWeakObjectPtr<APlayerController> PlayerControllerWeakPtr;
    UObject* WorldContextObject;

private:

    void OnLoginCompleteDelegate(int32 LocalPlayerId, bool bWasSuccessful, const FUniqueNetId& UserId,
                                 const FString& ErrorString);

    UFUNCTION()
    void OnLoginComplete();

    UFUNCTION()
    void OnLoginFailed(const FString& ErrorMessage);
    
    void EndTask();
};


UENUM(BlueprintType)
enum class EConnectLoginType : uint8
{
    Epic,
    Steam,
    PSN UMETA(DisplayName = "Playstation Network (PSN)"),
    XBL UMETA(DisplayName = "XBox Live (XBL)"),
    GOG,
    Discord,
    Nintendo,
    NintendoNSA UMETA(DisplayName = "Nintendo Service Account"),
    UPlay,
    OpenID UMETA(DisplayName = "OpenID Connect"),
    DeviceId,
    Apple
};

/**
 * Exposes the epic account login flow to blueprints
 */
UCLASS(BlueprintType, meta = (ExposedAsyncProxy = AsyncTask))
class ONLINESUBSYSTEMEPIC_API UEpicOnlineServicesConnectLoginTask
    : public UEpicOnlineServicesIdentityTask
{
    GENERATED_BODY()
private:
    int32 LocalUserNum;
    bool bCreateNewAccount;
    FOnlineAccountCredentials Credentials;

    void OnLoginCompleteDelegate(int32 LocalPlayerId, bool bWasSuccessful, const FUniqueNetId& UserId,
                                 const FString& ErrorString);

    void EndTask();

public:

    UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
    FOnLoginResultPin OnLoginSuccess;

    UPROPERTY(BlueprintAssignable, Category = "Epic Online Services|Identity")
    FOnLoginResultPin OnLoginFailure;

    // Attempts to login using user supplied credentials
    UFUNCTION(BlueprintCallable, Category = "Epic Online Services|Identity", meta = (BlueprintInternalUseOnly = "true",
        DisplayName = "Connect Login"))
    static UEpicOnlineServicesConnectLoginTask* TryLogin(int32 LocalUserNum, EConnectLoginType LoginType, FString UserId,
                                                         FString Token, bool bCreateNew);

    static FString ConnectLoginTypeToString(EConnectLoginType LoginType);

    /** UBlueprintAsyncActionBase interface */
    virtual void Activate() override;
};
