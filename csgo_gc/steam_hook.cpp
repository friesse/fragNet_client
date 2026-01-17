// this file sucks, don't scroll down!!! all you need to know is
// that this is the bridge betweem the game and ClientGC/ServerGC
#include "stdafx.h"
#include "steam_hook.h"
#include "gc_client.h"
#include "gc_server.h"
#include "platform.h"
#include <funchook.h>

// defines STEAM_PRIVATE_API
//#include <steam/steam_api_common.h> SINCE WE ARE USING THE OLD API WE NEED TO REMOVE THIS UNFORTUNATELY (KILL ME)

#undef STEAM_PRIVATE_API // we need these public so we can proxy them
#define STEAM_PRIVATE_API(...) __VA_ARGS__

// mikkotodo update the sdk... DONT UPDATE IT PLEASE
struct SteamNetworkingIdentity;

#include <steam/steam_api.h>
#include <steam/steam_gameserver.h>
#include <steam/isteamgamecoordinator.h>
#include <steam/isteamgameserver.h>

// these are in file scope for networking, callbacks and gc server
// client connect/disconnect notifications
static ClientGC *s_clientGC;
static ServerGC *s_serverGC;

template<size_t N>
inline bool InterfaceMatches(const char *name, const char(&compare)[N])
{
    size_t length = strlen(name);
    if (length != (N - 1))
    {
        return false;
    }

    // compare the full version
    if (!memcmp(name, compare, length))
    {
        return true; // matches
    }

    // compare the base name without the last 3 digits
    if (!memcmp(name, compare, length - 3))
    {
        Platform::Print("Got interface version %s, expecting %s\n", name, compare);
        return false; // not a match
    }

    return false; // not a match
}

// this class sucks but we need to do it this way because
class SteamGameCoordinatorProxy final : public ISteamGameCoordinator
{
    const bool m_server;

public:
    SteamGameCoordinatorProxy(uint64_t steamId)
        : m_server{ !steamId }
    {
        if (m_server)
        {
            Platform::Print("Creating ServerGC = %p\n", s_serverGC);
            assert(!s_serverGC);
            s_serverGC = new ServerGC;
        }
        else
        {
            assert(!s_clientGC);
            s_clientGC = new ClientGC{ steamId };
        }
    }

    ~SteamGameCoordinatorProxy()
    {
        if (m_server)
        {
            Platform::Print("Destroying ServerGC = %p\n", s_serverGC);
            assert(s_serverGC);
            delete s_serverGC;
            s_serverGC = nullptr;
        }
        else
        {
            assert(s_clientGC);
            delete s_clientGC;
            s_clientGC = nullptr;
        }
    }

    EGCResults SendMessage(uint32 unMsgType, const void *pubData, uint32 cubData) override
    {
        if (m_server)
        {
            assert(s_serverGC);
            s_serverGC->HandleMessage(unMsgType, pubData, cubData);
        }
        else
        {
            assert(s_clientGC);
            s_clientGC->HandleMessage(unMsgType, pubData, cubData);
        }

        switch (unMsgType) {
        case k_EMsgGCCStrike15_v2_ClientReportServer:
            Platform::Print("Reported server... message declined.\n");
            return k_EGCResultInvalidMessage;
        case k_EMsgGCCStrike15_v2_ClientReportPlayer:
            Platform::Print("Reported player... message declined.\n");
            return k_EGCResultInvalidMessage;
        }

        return k_EGCResultOK;
    }

    bool IsMessageAvailable(uint32 *pcubMsgSize) override
    {
        if (m_server)
        {
            return s_serverGC->HasOutgoingMessages(*pcubMsgSize);
        }
        else
        {
            return s_clientGC->HasOutgoingMessages(*pcubMsgSize);
        }
    }

    EGCResults RetrieveMessage(uint32 *punMsgType, void *pubDest, uint32 cubDest, uint32 *pcubMsgSize) override
    {
        bool result;

        if (m_server)
        {
            result = s_serverGC->PopOutgoingMessage(*punMsgType, pubDest, cubDest, *pcubMsgSize);
        }
        else
        {
            result = s_clientGC->PopOutgoingMessage(*punMsgType, pubDest, cubDest, *pcubMsgSize);
        }

        if (!result)
        {
            if (cubDest < *pcubMsgSize)
            {
                return k_EGCResultBufferTooSmall;
            }

            return k_EGCResultNoMessage;
        }

        return k_EGCResultOK;
    }
};

// stupid hack
constexpr SteamAPICall_t CheckSignatureCall = 0x6666666666666666;

// hook so we can spoof the dll signature checks and get rid of the annoying as fuck insecure message box
class SteamUtilsProxy final : public ISteamUtils
{
private:
    ISteamUtils *const m_original;

protected:
    void RunFrame() override {} // Keep this for ISteamUtils

public:
    virtual ~SteamUtilsProxy() = default; // add virtual destructor

    SteamUtilsProxy(ISteamUtils *original)
        : m_original{ original }
    {
    }

    uint32 GetSecondsSinceAppActive() override
    {
        return m_original->GetSecondsSinceAppActive();
    }

    uint32 GetSecondsSinceComputerActive() override
    {
        return m_original->GetSecondsSinceComputerActive();
    }

    EUniverse GetConnectedUniverse() override
    {
        return m_original->GetConnectedUniverse();
    }

    uint32 GetServerRealTime() override
    {
        return m_original->GetServerRealTime();
    }

    const char *GetIPCountry() override
    {
        return m_original->GetIPCountry();
    }

    bool GetImageSize(int iImage, uint32 *pnWidth, uint32 *pnHeight) override
    {
        return m_original->GetImageSize(iImage, pnWidth, pnHeight);
    }

    bool GetImageRGBA(int iImage, uint8 *pubDest, int nDestBufferSize) override
    {
        return m_original->GetImageRGBA(iImage, pubDest, nDestBufferSize);
    }

    bool GetCSERIPPort(uint32 *unIP, uint16 *usPort) override
    {
        return m_original->GetCSERIPPort(unIP, usPort);
    }

    uint8 GetCurrentBatteryPower() override
    {
        return m_original->GetCurrentBatteryPower();
    }

    uint32 GetAppID() override
    {
        return m_original->GetAppID();
    }

    void SetOverlayNotificationPosition(ENotificationPosition eNotificationPosition) override
    {
        m_original->SetOverlayNotificationPosition(eNotificationPosition);
    }

    /*bool DismissFloatingGamepadTextInput() override
    {
        return m_original->DismissFloatingGamepadTextInput();
    }*/

    bool IsAPICallCompleted(SteamAPICall_t hSteamAPICall, bool *pbFailed) override
    {
        if (hSteamAPICall == CheckSignatureCall)
        {
            if (pbFailed)
            {
                *pbFailed = false;
            }

            return true;
        }

        return m_original->IsAPICallCompleted(hSteamAPICall, pbFailed);
    }

    ESteamAPICallFailure GetAPICallFailureReason(SteamAPICall_t hSteamAPICall) override
    {
        // yeah we won't get here
        //if (hSteamAPICall == CheckSignatureCall)
        //{
        //    // not properly handled, shouldn't get here
        //    assert(false);
        //    return k_ESteamAPICallFailureNone;
        //}

        return m_original->GetAPICallFailureReason(hSteamAPICall);
    }

    bool GetAPICallResult(SteamAPICall_t hSteamAPICall, void *pCallback, int cubCallback, int iCallbackExpected, bool *pbFailed) override
    {
        if (hSteamAPICall == CheckSignatureCall
            && cubCallback == sizeof(CheckFileSignature_t)
            && iCallbackExpected == CheckFileSignature_t::k_iCallback)
        {
            if (pbFailed)
            {
                *pbFailed = false;
            }

            CheckFileSignature_t result{};
            result.m_eCheckFileSignature = k_ECheckFileSignatureNoSignaturesFoundForThisApp;
            memcpy(pCallback, &result, sizeof(result));
            return true;
        }

        return m_original->GetAPICallResult(hSteamAPICall, pCallback, cubCallback, iCallbackExpected, pbFailed);
    }

    // Remove this override since RunFrame is private/deprecated
    
    /*void RunFrame() override
    {
        m_original->RunFrame();
    }*/
    

    uint32 GetIPCCallCount() override
    {
        return m_original->GetIPCCallCount();
    }

    void SetWarningMessageHook(SteamAPIWarningMessageHook_t pFunction) override
    {
        m_original->SetWarningMessageHook(pFunction);
    }

    bool IsOverlayEnabled() override
    {
        return m_original->IsOverlayEnabled();
    }

    bool BOverlayNeedsPresent() override
    {
        return m_original->BOverlayNeedsPresent();
    }

    SteamAPICall_t CheckFileSignature([[maybe_unused]] const char *szFileName) override
    {
        // spoof this
        return CheckSignatureCall;
    }

    bool ShowGamepadTextInput(EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode, const char *pchDescription, uint32 unCharMax, const char *pchExistingText) override
    {
        return m_original->ShowGamepadTextInput(eInputMode, eLineInputMode, pchDescription, unCharMax, pchExistingText);
    }

    uint32 GetEnteredGamepadTextLength() override
    {
        return m_original->GetEnteredGamepadTextLength();
    }

    bool GetEnteredGamepadTextInput(char *pchText, uint32 cchText) override
    {
        return m_original->GetEnteredGamepadTextInput(pchText, cchText);
    }

    const char *GetSteamUILanguage() override
    {
        return m_original->GetSteamUILanguage();
    }

    bool IsSteamRunningInVR() override
    {
        return m_original->IsSteamRunningInVR();
    }

    void SetOverlayNotificationInset(int nHorizontalInset, int nVerticalInset) override
    {
        m_original->SetOverlayNotificationInset(nHorizontalInset, nVerticalInset);
    }

    bool IsSteamInBigPictureMode() override
    {
        return m_original->IsSteamInBigPictureMode();
    }

    void StartVRDashboard() override
    {
        m_original->StartVRDashboard();
    }

    /*bool IsVRHeadsetStreamingEnabled() override
    {
        return m_original->IsVRHeadsetStreamingEnabled();
    }

    void SetVRHeadsetStreamingEnabled(bool bEnabled) override
    {
        m_original->SetVRHeadsetStreamingEnabled(bEnabled);
    }

    bool IsSteamChinaLauncher() override
    {
        return m_original->IsSteamChinaLauncher();
    }

    bool InitFilterText(uint32 unFilterOptions) override
    {
        return m_original->InitFilterText(unFilterOptions);
    }

    int FilterText(ETextFilteringContext eContext, CSteamID sourceSteamID, const char *pchInputMessage, char *pchOutFilteredText, uint32 nByteSizeOutFilteredText) override
    {
        return m_original->FilterText(eContext, sourceSteamID, pchInputMessage, pchOutFilteredText, nByteSizeOutFilteredText);
    }

    ESteamIPv6ConnectivityState GetIPv6ConnectivityState(ESteamIPv6ConnectivityProtocol eProtocol) override
    {
        return m_original->GetIPv6ConnectivityState(eProtocol);
    }

    bool IsSteamRunningOnSteamDeck() override
    {
        return m_original->IsSteamRunningOnSteamDeck();
    }

    bool ShowFloatingGamepadTextInput(EFloatingGamepadTextInputMode eKeyboardMode, int nTextFieldXPosition, int nTextFieldYPosition, int nTextFieldWidth, int nTextFieldHeight) override
    {
        return m_original->ShowFloatingGamepadTextInput(eKeyboardMode, nTextFieldXPosition, nTextFieldYPosition, nTextFieldWidth, nTextFieldHeight);
    }

    void SetGameLauncherMode(bool bLauncherMode) override
    {
        m_original->SetGameLauncherMode(bLauncherMode);
    }*/
};

class SteamInventoryProxy : public ISteamInventory
{
private:
    ISteamInventory* m_original;

public:
    SteamInventoryProxy(ISteamInventory* original) 
        : m_original{ original } 
    {
    }

    EResult GetResultStatus(SteamInventoryResult_t resultHandle) override {
        Platform::Print("SteamInventoryProxy {GetResultStatus} {%lu}", resultHandle);
        return m_original->GetResultStatus(resultHandle);
    }
    bool GetResultItems(SteamInventoryResult_t resultHandle, SteamItemDetails_t* pOutItemsArray, uint32* punOutItemsArraySize) override {
        Platform::Print("SteamInventoryProxy {GetResultItems} {%lu, %lu, %lu}", resultHandle, pOutItemsArray, punOutItemsArraySize);
        return m_original->GetResultItems(resultHandle, pOutItemsArray, punOutItemsArraySize);
    }
    uint32 GetResultTimestamp(SteamInventoryResult_t resultHandle) override {
        Platform::Print("SteamInventoryProxy {GetResultTimestamp} {%lu}", resultHandle);
        return m_original->GetResultTimestamp(resultHandle);
    }
    bool CheckResultSteamID(SteamInventoryResult_t resultHandle, CSteamID steamIDExpected) override {
        Platform::Print("SteamInventoryProxy {CheckResultSteamID} {%lu, %llu}", resultHandle, steamIDExpected.ConvertToUint64());
        return m_original->CheckResultSteamID(resultHandle, steamIDExpected);
    }
    void DestroyResult(SteamInventoryResult_t resultHandle) override {
        Platform::Print("SteamInventoryProxy {DestroyResult} {%lu}", resultHandle);
        return m_original->DestroyResult(resultHandle);
    }
    bool GetAllItems(SteamInventoryResult_t* pResultHandle) override {
        Platform::Print("SteamInventoryProxy {GetAllItems} {%lu}", pResultHandle);
        return m_original->GetAllItems(pResultHandle);
    }
    bool GetItemsByID(SteamInventoryResult_t* pResultHandle, const SteamItemInstanceID_t* pInstanceIDs, uint32 unCountInstanceIDs) override {
        Platform::Print("SteamInventoryProxy {GetItemsByID} {%lu, %llu, %lu}", pResultHandle, pInstanceIDs, unCountInstanceIDs);
        return m_original->GetItemsByID(pResultHandle, pInstanceIDs, unCountInstanceIDs);
    }
    bool SerializeResult(SteamInventoryResult_t resultHandle, void* pOutBuffer, uint32* punOutBufferSize) override {
        Platform::Print("SteamInventoryProxy {SerializeResult} {%lu, %lu, %lu}", resultHandle, pOutBuffer, punOutBufferSize);
        return m_original->SerializeResult(resultHandle, pOutBuffer, punOutBufferSize);
    }
    bool DeserializeResult(SteamInventoryResult_t* pOutResultHandle, const void* pBuffer, uint32 unBufferSize, bool bRESERVED_MUST_BE_FALSE = false) override {
        Platform::Print("SteamInventoryProxy {DeserializeResult} {%lu, %lu, %lu, %i}", pOutResultHandle, pBuffer, unBufferSize, bRESERVED_MUST_BE_FALSE);
        return m_original->DeserializeResult(pOutResultHandle, pBuffer, unBufferSize, bRESERVED_MUST_BE_FALSE);
    }
    bool GenerateItems(SteamInventoryResult_t* pResultHandle, const SteamItemDef_t* pArrayItemDefs, const uint32* punArrayQuantity, uint32 unArrayLength) override {
        Platform::Print("SteamInventoryProxy {GenerateItems} {%lu, %lu, %lu, %lu}", pResultHandle, pArrayItemDefs, punArrayQuantity, unArrayLength);
        return m_original->GenerateItems(pResultHandle, pArrayItemDefs, punArrayQuantity, unArrayLength);
    }
    bool GrantPromoItems(SteamInventoryResult_t* pResultHandle) override {
        Platform::Print("SteamInventoryProxy {GrantPromoItems} {%lu}", pResultHandle);
        return m_original->GrantPromoItems(pResultHandle);
    }
    bool AddPromoItem(SteamInventoryResult_t* pResultHandle, SteamItemDef_t itemDef) override {
        Platform::Print("SteamInventoryProxy {AddPromoItem} {%lu, %lu}", pResultHandle, itemDef);
        return m_original->AddPromoItem(pResultHandle, itemDef);
    }
    bool AddPromoItems(SteamInventoryResult_t* pResultHandle, const SteamItemDef_t* pArrayItemDefs, uint32 unArrayLength) override {
        Platform::Print("SteamInventoryProxy {AddPromoItems} {%lu, %lu, %lu}", pResultHandle, pArrayItemDefs, unArrayLength);
        return m_original->AddPromoItems(pResultHandle, pArrayItemDefs, unArrayLength);
    }
    bool ConsumeItem(SteamInventoryResult_t* pResultHandle, SteamItemInstanceID_t itemConsume, uint32 unQuantity) override {
        Platform::Print("SteamInventoryProxy {ConsumeItem} {%lu, %llu, %lu}", pResultHandle, itemConsume, unQuantity);
        return m_original->ConsumeItem(pResultHandle, itemConsume, unQuantity);
    }
    bool ExchangeItems(SteamInventoryResult_t* pResultHandle, const SteamItemDef_t* pArrayGenerate, const uint32* punArrayGenerateQuantity, uint32 unArrayGenerateLength, const SteamItemInstanceID_t* pArrayDestroy, const uint32* punArrayDestroyQuantity, uint32 unArrayDestroyLength) override {
        Platform::Print("SteamInventoryProxy {ExchangeItems} {%lu, %lu, %lu, %lu, %llu, %lu, %lu}", pResultHandle, pArrayGenerate, punArrayGenerateQuantity, unArrayGenerateLength, pArrayDestroy, punArrayDestroyQuantity, unArrayDestroyLength);
        return m_original->ExchangeItems(pResultHandle, pArrayGenerate, punArrayGenerateQuantity, unArrayGenerateLength, pArrayDestroy, punArrayDestroyQuantity, unArrayDestroyLength);
    }
    bool TransferItemQuantity(SteamInventoryResult_t* pResultHandle, SteamItemInstanceID_t itemIdSource, uint32 unQuantity, SteamItemInstanceID_t itemIdDest) override {
        Platform::Print("SteamInventoryProxy {TransferItemQuantity} {%lu, %llu, %lu, %llu}", pResultHandle, itemIdSource, unQuantity, itemIdDest);
        return m_original->TransferItemQuantity(pResultHandle, itemIdSource, unQuantity, itemIdDest);
    }
    void SendItemDropHeartbeat() override {
        Platform::Print("SteamInventoryProxy {SendItemDropHeartbeat}");
        return m_original->SendItemDropHeartbeat();
    }
    bool TriggerItemDrop(SteamInventoryResult_t* pResultHandle, SteamItemDef_t dropListDefinition) override {
        Platform::Print("SteamInventoryProxy {TriggerItemDrop} {%lu, %lu}", pResultHandle, dropListDefinition);
        return m_original->TriggerItemDrop(pResultHandle, dropListDefinition);
    }
    bool TradeItems(SteamInventoryResult_t* pResultHandle, CSteamID steamIDTradePartner, const SteamItemInstanceID_t* pArrayGive, const uint32* pArrayGiveQuantity, uint32 nArrayGiveLength, const SteamItemInstanceID_t* pArrayGet, const uint32* pArrayGetQuantity, uint32 nArrayGetLength) override {
        Platform::Print("SteamInventoryProxy {TradeItems} {%lu, %llu, %llu, %lu, %lu, %lu}", pResultHandle, steamIDTradePartner.ConvertToUint64(), pArrayGive, pArrayGiveQuantity, nArrayGiveLength, pArrayGet, pArrayGetQuantity, nArrayGetLength);
        return m_original->TradeItems(pResultHandle, steamIDTradePartner, pArrayGive, pArrayGiveQuantity, nArrayGiveLength, pArrayGet, pArrayGetQuantity, nArrayGetLength);
    }
    bool LoadItemDefinitions() override {
        Platform::Print("SteamInventoryProxy {LoadItemDefinitions}");
        return m_original->LoadItemDefinitions();
    }
    bool GetItemDefinitionIDs(SteamItemDef_t* pItemDefIDs, uint32* punItemDefIDsArraySize) override {
        Platform::Print("SteamInventoryProxy {GetItemDefinitionIDs} {%lu, %s}", pItemDefIDs, punItemDefIDsArraySize);
        return m_original->GetItemDefinitionIDs(pItemDefIDs, punItemDefIDsArraySize);
    }
    bool GetItemDefinitionProperty(SteamItemDef_t iDefinition, const char* pchPropertyName, char* pchValueBuffer, uint32* punValueBufferSize) override {
        Platform::Print("SteamInventoryProxy {GetItemDefinitionProperty} {%lu, %s, %s, %lu}", iDefinition, pchPropertyName, pchValueBuffer, punValueBufferSize);
        return m_original->GetItemDefinitionProperty(iDefinition, pchPropertyName, pchValueBuffer, punValueBufferSize);
    }

};

class SteamGameServerProxy final : public ISteamGameServer
{
private:
    ISteamGameServer *m_original;

public:
    virtual ~SteamGameServerProxy() = default; // Add virtual destructor

    SteamGameServerProxy(ISteamGameServer *original)
        : m_original{ original }
    {
    }

    bool InitGameServer(uint32 unIP, uint16 usGamePort, uint16 usQueryPort, uint32 unFlags, AppId_t nGameAppId, const char *pchVersionString) override
    {
        // no(yes) longer present in steamworks sdk
        //constexpr uint32 k_unServerFlagSecure = 2;

        // never run secure!!!
        unFlags &= ~k_unServerFlagSecure;

        return m_original->InitGameServer(unIP, usGamePort, usQueryPort, unFlags, nGameAppId, pchVersionString);
    }

    void SetProduct(const char *pszProduct) override
    {
        m_original->SetProduct(pszProduct);
    }

    void SetGameDescription(const char *pszGameDescription) override
    {
        m_original->SetGameDescription(pszGameDescription);
    }

    void SetModDir(const char *pszModDir) override
    {
        m_original->SetModDir(pszModDir);
    }

    void SetDedicatedServer(bool bDedicated) override
    {
        m_original->SetDedicatedServer(bDedicated);
    }

    void LogOn(const char *pszToken) override
    {
        m_original->LogOn(pszToken);
    }

    void LogOnAnonymous() override
    {
        m_original->LogOnAnonymous();
    }

    void LogOff() override
    {
        m_original->LogOff();
    }

    bool BLoggedOn() override
    {
        return m_original->BLoggedOn();
    }

    bool BSecure() override
    {
        return m_original->BSecure();
    }

    CSteamID GetSteamID() override
    {
        return m_original->GetSteamID();
    }

    bool WasRestartRequested() override
    {
        // mikko: quiet the "your server is out of date please update and restart" spew
        //return m_original->WasRestartRequested();
        return false;
    }

    void SetMaxPlayerCount(int cPlayersMax) override
    {
        m_original->SetMaxPlayerCount(cPlayersMax);
    }

    void SetBotPlayerCount(int cBotplayers) override
    {
        m_original->SetBotPlayerCount(cBotplayers);
    }

    void SetServerName(const char *pszServerName) override
    {
        m_original->SetServerName(pszServerName);
    }

    void SetMapName(const char *pszMapName) override
    {
        m_original->SetMapName(pszMapName);
    }

    void SetPasswordProtected(bool bPasswordProtected) override
    {
        m_original->SetPasswordProtected(bPasswordProtected);
    }

    void SetSpectatorPort(uint16 unSpectatorPort) override
    {
        m_original->SetSpectatorPort(unSpectatorPort);
    }

    void SetSpectatorServerName(const char *pszSpectatorServerName) override
    {
        m_original->SetSpectatorServerName(pszSpectatorServerName);
    }

    void ClearAllKeyValues() override
    {
        m_original->ClearAllKeyValues();
    }

    void SetKeyValue(const char *pKey, const char *pValue) override
    {
        m_original->SetKeyValue(pKey, pValue);
    }

    void SetGameTags(const char *pchGameTags) override
    {
        m_original->SetGameTags(pchGameTags);
    }

    void SetGameData(const char *pchGameData) override
    {
        m_original->SetGameData(pchGameData);
    }

    void SetRegion(const char *pszRegion) override
    {
        m_original->SetRegion(pszRegion);
    }

    /*void SetAdvertiseServerActive(bool bActive) override
    {
        m_original->SetAdvertiseServerActive(bActive);
    }*/

    // Old version of GetAuthSessionTicket only had 3 parameters
    HAuthTicket GetAuthSessionTicket(void* pTicket, int cbMaxTicket, uint32* pcbTicket) override
    {
        return m_original->GetAuthSessionTicket(pTicket, cbMaxTicket, pcbTicket);
    }

    EBeginAuthSessionResult BeginAuthSession(const void *pAuthTicket, int cbAuthTicket, CSteamID steamID) override
    {
        EBeginAuthSessionResult result = m_original->BeginAuthSession(pAuthTicket, cbAuthTicket, steamID);
        if (s_serverGC && result == k_EBeginAuthSessionResultOK)
        {
            s_serverGC->ClientConnected(steamID.ConvertToUint64(), pAuthTicket, cbAuthTicket);
        }

        return result;
    }

    void EndAuthSession(CSteamID steamID) override
    {
        if (s_serverGC)
        {
            s_serverGC->ClientDisconnected(steamID.ConvertToUint64());
        }

        m_original->EndAuthSession(steamID);
    }

    void CancelAuthTicket(HAuthTicket hAuthTicket) override
    {
        m_original->CancelAuthTicket(hAuthTicket);
    }

    EUserHasLicenseForAppResult UserHasLicenseForApp(CSteamID steamID, AppId_t appID) override
    {
        return m_original->UserHasLicenseForApp(steamID, appID);
    }

    bool RequestUserGroupStatus(CSteamID steamIDUser, CSteamID steamIDGroup) override
    {
        return m_original->RequestUserGroupStatus(steamIDUser, steamIDGroup);
    }

    void GetGameplayStats() override
    {
        m_original->GetGameplayStats();
    }

    SteamAPICall_t GetServerReputation() override
    {
        return m_original->GetServerReputation();
    }

    uint32 GetPublicIP() override
    {
        return m_original->GetPublicIP();
    }

    bool HandleIncomingPacket(const void *pData, int cbData, uint32 srcIP, uint16 srcPort) override
    {
        return m_original->HandleIncomingPacket(pData, cbData, srcIP, srcPort);
    }

    int GetNextOutgoingPacket(void *pOut, int cbMaxOut, uint32 *pNetAdr, uint16 *pPort) override
    {
        return m_original->GetNextOutgoingPacket(pOut, cbMaxOut, pNetAdr, pPort);
    }

    SteamAPICall_t AssociateWithClan(CSteamID steamIDClan) override
    {
        return m_original->AssociateWithClan(steamIDClan);
    }

    SteamAPICall_t ComputeNewPlayerCompatibility(CSteamID steamIDNewPlayer) override
    {
        return m_original->ComputeNewPlayerCompatibility(steamIDNewPlayer);
    }

    bool SendUserConnectAndAuthenticate(uint32 unIPClient, const void *pvAuthBlob, uint32 cubAuthBlobSize, CSteamID *pSteamIDUser) override
    {
        return m_original->SendUserConnectAndAuthenticate(unIPClient, pvAuthBlob, cubAuthBlobSize, pSteamIDUser);
    }

    CSteamID CreateUnauthenticatedUserConnection() override
    {
        return m_original->CreateUnauthenticatedUserConnection();
    }

    void SendUserDisconnect(CSteamID steamIDUser) override
    {
        m_original->SendUserDisconnect(steamIDUser);
    }

    bool BUpdateUserData(CSteamID steamIDUser, const char *pchPlayerName, uint32 uScore) override
    {
        return m_original->BUpdateUserData(steamIDUser, pchPlayerName, uScore);
    }

    /*void SetMasterServerHeartbeatInterval(int iHeartbeatInterval) override
    {
        m_original->SetMasterServerHeartbeatInterval(iHeartbeatInterval);
    }

    void ForceMasterServerHeartbeat() override
    {
        m_original->ForceMasterServerHeartbeat();
    }*/

    void EnableHeartbeats(bool bActive) override
    {
        m_original->EnableHeartbeats(bActive);
    }

    void SetHeartbeatInterval(int iHeartbeatInterval) override
    {
        m_original->SetHeartbeatInterval(iHeartbeatInterval);
    }

    void ForceHeartbeat() override
    {
        m_original->ForceHeartbeat();
    }
};

class SteamFriendsProxy final : public ISteamFriends
{
    ISteamFriends* m_original;
    bool m_isIngame = false; // for our rpc

public:
    SteamFriendsProxy(ISteamFriends* original)
        : m_original{ original }
    {
    }

    bool SetRichPresence(const char* pchKey, const char* pchValue) override
    {
        const char* modifiedKey = pchKey;
        const char* modifiedValue = pchValue;

        if (strcmp(pchKey, "status") == 0)
        {
            constexpr const char* COMMUNITY_PREFIX = "Community ";
            const size_t prefixLen = strlen(COMMUNITY_PREFIX);

            if (pchValue && strncmp(pchValue, COMMUNITY_PREFIX, prefixLen) == 0)
            {
                modifiedValue = pchValue + prefixLen;
                m_isIngame = true;
            }
            else
            {
                m_isIngame = false;
            }
        }

        if (m_isIngame)
        {
            if (strcmp(pchKey, "game:act") == 0)
            {
                modifiedValue = nullptr;
            }
            else if (strcmp(pchKey, "game:server") == 0 && pchValue)
            {
                if (strcmp(pchValue, "community") == 0)
                {
                    modifiedValue = "kv";
                }
            }
            else if (strcmp(pchKey, "game:score") == 0)
            {
                if (!pchValue || strcmp(pchValue, "(null)") == 0)
                {
                    modifiedValue = "- ClassicCounter";
                }
            }
        }

        //Platform::Print("key {%s}, value {%s}\n", modifiedKey, modifiedValue ? modifiedValue : "(null)");
        bool result = m_original->SetRichPresence(modifiedKey, modifiedValue);

        if (m_isIngame)
        {
            // set these if we have a map
            if (strcmp(pchKey, "game:map") == 0 && pchValue)
            {
                m_original->SetRichPresence("game:state", "game");
                m_original->SetRichPresence("steam_display", "#display_GameKnownMapScore");
            }
        }

        return result;
    }

    const char* GetPersonaName() override 
    { 
        return m_original->GetPersonaName(); 
    }

    SteamAPICall_t SetPersonaName(const char* pchPersonaName) override 
    { 
        return m_original->SetPersonaName(pchPersonaName); 
    }

    EPersonaState GetPersonaState() override 
    { 
        return m_original->GetPersonaState(); 
    }

    int GetFriendCount(int iFriendFlags) override 
    { 
        return m_original->GetFriendCount(iFriendFlags); 
    }

    CSteamID GetFriendByIndex(int iFriend, int iFriendFlags) override 
    { 
        return m_original->GetFriendByIndex(iFriend, iFriendFlags); 
    }

    EFriendRelationship GetFriendRelationship(CSteamID steamIDFriend) override 
    { 
        return m_original->GetFriendRelationship(steamIDFriend); 
    }

    EPersonaState GetFriendPersonaState(CSteamID steamIDFriend) override 
    { 
        return m_original->GetFriendPersonaState(steamIDFriend); 
    }

    const char* GetFriendPersonaName(CSteamID steamIDFriend) override 
    { 
        return m_original->GetFriendPersonaName(steamIDFriend); 
    }

    bool GetFriendGamePlayed(CSteamID steamIDFriend, OUT_STRUCT() FriendGameInfo_t* pFriendGameInfo) override 
    { 
        return m_original->GetFriendGamePlayed(steamIDFriend, pFriendGameInfo); 
    }

    const char* GetFriendPersonaNameHistory(CSteamID steamIDFriend, int iPersonaName) override 
    { 
        return m_original->GetFriendPersonaNameHistory(steamIDFriend, iPersonaName); 
    }

    int GetFriendSteamLevel(CSteamID steamIDFriend) override 
    { 
        return m_original->GetFriendSteamLevel(steamIDFriend); 
    }

    const char* GetPlayerNickname(CSteamID steamIDPlayer) override 
    { 
        return m_original->GetPlayerNickname(steamIDPlayer); 
    }

    int GetFriendsGroupCount() override 
    { 
        return m_original->GetFriendsGroupCount(); 
    }

    FriendsGroupID_t GetFriendsGroupIDByIndex(int iFG) override 
    { 
        return m_original->GetFriendsGroupIDByIndex(iFG); 
    }

    const char* GetFriendsGroupName(FriendsGroupID_t friendsGroupID) override 
    { 
        return m_original->GetFriendsGroupName(friendsGroupID); 
    }

    int GetFriendsGroupMembersCount(FriendsGroupID_t friendsGroupID) override 
    { 
        return m_original->GetFriendsGroupMembersCount(friendsGroupID); 
    }

    void GetFriendsGroupMembersList(FriendsGroupID_t friendsGroupID, OUT_ARRAY_CALL(nMembersCount, GetFriendsGroupMembersCount, friendsGroupID) CSteamID* pOutSteamIDMembers, int nMembersCount) override 
    { 
        return m_original->GetFriendsGroupMembersList(friendsGroupID, pOutSteamIDMembers, nMembersCount); 
    }

    bool HasFriend(CSteamID steamIDFriend, int iFriendFlags) override 
    { 
        return m_original->HasFriend(steamIDFriend, iFriendFlags); 
    }

    int GetClanCount() override 
    { 
        return m_original->GetClanCount(); 
    }

    CSteamID GetClanByIndex(int iClan) override 
    { 
        return m_original->GetClanByIndex(iClan); 
    }

    const char* GetClanName(CSteamID steamIDClan) override 
    { 
        return m_original->GetClanName(steamIDClan); 
    }

    const char* GetClanTag(CSteamID steamIDClan) override 
    { 
        return m_original->GetClanTag(steamIDClan); 
    }

    bool GetClanActivityCounts(CSteamID steamIDClan, int* pnOnline, int* pnInGame, int* pnChatting) override 
    {
        return m_original->GetClanActivityCounts(steamIDClan, pnOnline, pnInGame, pnChatting); 
    }

    SteamAPICall_t DownloadClanActivityCounts(ARRAY_COUNT(cClansToRequest) CSteamID* psteamIDClans, int cClansToRequest) override 
    { 
        return m_original->DownloadClanActivityCounts(psteamIDClans, cClansToRequest); 
    }

    int GetFriendCountFromSource(CSteamID steamIDSource) override 
    { 
        return m_original->GetFriendCountFromSource(steamIDSource); 
    }

    CSteamID GetFriendFromSourceByIndex(CSteamID steamIDSource, int iFriend) override 
    { 
        return m_original->GetFriendFromSourceByIndex(steamIDSource, iFriend); 
    }

    bool IsUserInSource(CSteamID steamIDUser, CSteamID steamIDSource) override 
    { 
        return m_original->IsUserInSource(steamIDUser, steamIDSource); 
    }

    void SetInGameVoiceSpeaking(CSteamID steamIDUser, bool bSpeaking) override 
    { 
        return m_original->SetInGameVoiceSpeaking(steamIDUser, bSpeaking); 
    }

    void ActivateGameOverlay(const char* pchDialog) override 
    { 
        return m_original->ActivateGameOverlay(pchDialog); 
    }

    void ActivateGameOverlayToUser(const char* pchDialog, CSteamID steamID) override 
    { 
        return m_original->ActivateGameOverlayToUser(pchDialog, steamID); 
    }

    void ActivateGameOverlayToWebPage(const char* pchURL) override 
    { 
        return m_original->ActivateGameOverlayToWebPage(pchURL); 
    }

    void ActivateGameOverlayToStore(AppId_t nAppID, EOverlayToStoreFlag eFlag) override 
    { 
        return m_original->ActivateGameOverlayToStore(nAppID, eFlag); 
    }

    void SetPlayedWith(CSteamID steamIDUserPlayedWith) override 
    { 
        return m_original->SetPlayedWith(steamIDUserPlayedWith); 
    }

    void ActivateGameOverlayInviteDialog(CSteamID steamIDLobby) override 
    { 
        return m_original->ActivateGameOverlayInviteDialog(steamIDLobby); 
    }

    int GetSmallFriendAvatar(CSteamID steamIDFriend) override 
    { 
        return m_original->GetSmallFriendAvatar(steamIDFriend); 
    }

    int GetMediumFriendAvatar(CSteamID steamIDFriend) override 
    { 
        return m_original->GetMediumFriendAvatar(steamIDFriend); 
    }

    int GetLargeFriendAvatar(CSteamID steamIDFriend) override 
    { 
        return m_original->GetLargeFriendAvatar(steamIDFriend); 
    }

    bool RequestUserInformation(CSteamID steamIDUser, bool bRequireNameOnly) override 
    { 
        return m_original->RequestUserInformation(steamIDUser, bRequireNameOnly); 
    }

    SteamAPICall_t RequestClanOfficerList(CSteamID steamIDClan) override 
    { 
        return m_original->RequestClanOfficerList(steamIDClan);
    }

    CSteamID GetClanOwner(CSteamID steamIDClan) override 
    { 
        return m_original->GetClanOwner(steamIDClan); 
    }

    int GetClanOfficerCount(CSteamID steamIDClan) override 
    { 
        return m_original->GetClanOfficerCount(steamIDClan); 
    }

    CSteamID GetClanOfficerByIndex(CSteamID steamIDClan, int iOfficer) override 
    { 
        return m_original->GetClanOfficerByIndex(steamIDClan, iOfficer); 
    }

    uint32 GetUserRestrictions() override 
    { 
        return m_original->GetUserRestrictions(); 
    }

    /*bool SetRichPresence(const char* pchKey, const char* pchValue) override 
    { 
        return m_original->SetRichPresence(pchKey, pchValue); 
    }*/

    void ClearRichPresence() override 
    { 
        return m_original->ClearRichPresence(); 
    }

    const char* GetFriendRichPresence(CSteamID steamIDFriend, const char* pchKey) override 
    { 
        return m_original->GetFriendRichPresence(steamIDFriend, pchKey); 
    }

    int GetFriendRichPresenceKeyCount(CSteamID steamIDFriend) override 
    { 
        return m_original->GetFriendRichPresenceKeyCount(steamIDFriend); 
    }

    const char* GetFriendRichPresenceKeyByIndex(CSteamID steamIDFriend, int iKey) override 
    { 
        return m_original->GetFriendRichPresenceKeyByIndex(steamIDFriend, iKey); 
    }

    void RequestFriendRichPresence(CSteamID steamIDFriend) override 
    { 
        return m_original->RequestFriendRichPresence(steamIDFriend); 
    }

    bool InviteUserToGame(CSteamID steamIDFriend, const char* pchConnectString) override 
    { 
        return m_original->InviteUserToGame(steamIDFriend, pchConnectString); 
    }

    int GetCoplayFriendCount() override 
    { 
        return m_original->GetCoplayFriendCount(); 
    }

    CSteamID GetCoplayFriend(int iCoplayFriend) override 
    { 
        return m_original->GetCoplayFriend(iCoplayFriend); 
    }

    int GetFriendCoplayTime(CSteamID steamIDFriend) override 
    { 
        return m_original->GetFriendCoplayTime(steamIDFriend); 
    }

    AppId_t GetFriendCoplayGame(CSteamID steamIDFriend) override 
    { 
        return m_original->GetFriendCoplayGame(steamIDFriend); 
    }

    SteamAPICall_t JoinClanChatRoom(CSteamID steamIDClan) override 
    { 
        return m_original->JoinClanChatRoom(steamIDClan); 
    }

    bool LeaveClanChatRoom(CSteamID steamIDClan) override 
    { 
        return m_original->LeaveClanChatRoom(steamIDClan); 
    }

    int GetClanChatMemberCount(CSteamID steamIDClan) override 
    { 
        return m_original->GetClanChatMemberCount(steamIDClan); 
    }

    CSteamID GetChatMemberByIndex(CSteamID steamIDClan, int iUser) override 
    { 
        return m_original->GetChatMemberByIndex(steamIDClan, iUser); 
    }

    bool SendClanChatMessage(CSteamID steamIDClanChat, const char* pchText) override 
    { 
        return m_original->SendClanChatMessage(steamIDClanChat, pchText); 
    }

    int GetClanChatMessage(CSteamID steamIDClanChat, int iMessage, void* prgchText, int cchTextMax, EChatEntryType* peChatEntryType, OUT_STRUCT() CSteamID* psteamidChatter) override 
    { 
        return m_original->GetClanChatMessage(steamIDClanChat, iMessage, prgchText, cchTextMax, peChatEntryType, psteamidChatter); 
    }

    bool IsClanChatAdmin(CSteamID steamIDClanChat, CSteamID steamIDUser) override 
    { 
        return m_original->IsClanChatAdmin(steamIDClanChat, steamIDUser); 
    }

    bool IsClanChatWindowOpenInSteam(CSteamID steamIDClanChat) override 
    { 
        return m_original->IsClanChatWindowOpenInSteam(steamIDClanChat); 
    }

    bool OpenClanChatWindowInSteam(CSteamID steamIDClanChat) override 
    { 
        return m_original->OpenClanChatWindowInSteam(steamIDClanChat); 
    }

    bool CloseClanChatWindowInSteam(CSteamID steamIDClanChat) override 
    { 
        return m_original->CloseClanChatWindowInSteam(steamIDClanChat); 
    }

    bool SetListenForFriendsMessages(bool bInterceptEnabled) override 
    { 
        return m_original->SetListenForFriendsMessages(bInterceptEnabled); 
    }

    bool ReplyToFriendMessage(CSteamID steamIDFriend, const char* pchMsgToSend) override 
    { 
        return m_original->ReplyToFriendMessage(steamIDFriend, pchMsgToSend); 
    }

    int GetFriendMessage(CSteamID steamIDFriend, int iMessageID, void* pvData, int cubData, EChatEntryType* peChatEntryType) override 
    { 
        return m_original->GetFriendMessage(steamIDFriend, iMessageID, pvData, cubData, peChatEntryType); 
    }

    SteamAPICall_t GetFollowerCount(CSteamID steamID) override 
    { 
        return m_original->GetFollowerCount(steamID); 
    }

    SteamAPICall_t IsFollowing(CSteamID steamID) override 
    { 
        return m_original->IsFollowing(steamID); 
    }

    SteamAPICall_t EnumerateFollowingList(uint32 unStartIndex) override 
    { 
        return m_original->EnumerateFollowingList(unStartIndex); 
    }
};

class SteamUserProxy : public ISteamUser
{
    ISteamUser *m_original;

public:
    SteamUserProxy(ISteamUser *original)
        : m_original{ original }
    {
    }

    HSteamUser GetHSteamUser() override
    {
        return m_original->GetHSteamUser();
    }

    /*HAuthTicket GetAuthTicketForWebApi(const char* pchIdentity) override
    {
        return m_original->GetAuthTicketForWebApi(pchIdentity);
    }*/

    bool BLoggedOn() override
    {
        return m_original->BLoggedOn();
    }

    CSteamID GetSteamID() override
    {
        return m_original->GetSteamID();
    }

    int InitiateGameConnection(void *pAuthBlob, int cbMaxAuthBlob, CSteamID steamIDGameServer, uint32 unIPServer, uint16 usPortServer, bool bSecure) override
    {
        return m_original->InitiateGameConnection(pAuthBlob, cbMaxAuthBlob, steamIDGameServer, unIPServer, usPortServer, bSecure);
    }

    void TerminateGameConnection(uint32 unIPServer, uint16 usPortServer) override
    {
        m_original->TerminateGameConnection(unIPServer, usPortServer);
    }

    void TrackAppUsageEvent(CGameID gameID, int eAppUsageEvent, const char *pchExtraInfo) override
    {
        m_original->TrackAppUsageEvent(gameID, eAppUsageEvent, pchExtraInfo);
    }

    bool GetUserDataFolder(char *pchBuffer, int cubBuffer) override
    {
        return m_original->GetUserDataFolder(pchBuffer, cubBuffer);
    }

    void StartVoiceRecording() override
    {
        m_original->StartVoiceRecording();
    }

    void StopVoiceRecording() override
    {
        m_original->StopVoiceRecording();
    }

    EVoiceResult GetAvailableVoice(uint32 *pcbCompressed, uint32 *pcbUncompressed_Deprecated, uint32 nUncompressedVoiceDesiredSampleRate_Deprecated) override
    {
        return m_original->GetAvailableVoice(pcbCompressed, pcbUncompressed_Deprecated, nUncompressedVoiceDesiredSampleRate_Deprecated);
    }

    EVoiceResult GetVoice(bool bWantCompressed, void *pDestBuffer, uint32 cbDestBufferSize, uint32 *nBytesWritten, bool bWantUncompressed_Deprecated, void *pUncompressedDestBuffer_Deprecated, uint32 cbUncompressedDestBufferSize_Deprecated, uint32 *nUncompressBytesWritten_Deprecated, uint32 nUncompressedVoiceDesiredSampleRate_Deprecated) override
    {
        return m_original->GetVoice(bWantCompressed, pDestBuffer, cbDestBufferSize, nBytesWritten, bWantUncompressed_Deprecated, pUncompressedDestBuffer_Deprecated, cbUncompressedDestBufferSize_Deprecated, nUncompressBytesWritten_Deprecated, nUncompressedVoiceDesiredSampleRate_Deprecated);
    }

    EVoiceResult DecompressVoice(const void *pCompressed, uint32 cbCompressed, void *pDestBuffer, uint32 cbDestBufferSize, uint32 *nBytesWritten, uint32 nDesiredSampleRate) override
    {
        return m_original->DecompressVoice(pCompressed, cbCompressed, pDestBuffer, cbDestBufferSize, nBytesWritten, nDesiredSampleRate);
    }

    uint32 GetVoiceOptimalSampleRate() override
    {
        return m_original->GetVoiceOptimalSampleRate();
    }

    // this SEEMS important but its giving me errors
    HAuthTicket GetAuthSessionTicket(void* pTicket, int cbMaxTicket, uint32* pcbTicket) override
    {
        HAuthTicket ticket = m_original->GetAuthSessionTicket(pTicket, cbMaxTicket, pcbTicket);
        if (s_clientGC && ticket != k_HAuthTicketInvalid)
        {
            s_clientGC->SetAuthTicket(ticket, pTicket, *pcbTicket);
        }
        return ticket;
    }

    EBeginAuthSessionResult BeginAuthSession(const void *pAuthTicket, int cbAuthTicket, CSteamID steamID) override
    {
        return m_original->BeginAuthSession(pAuthTicket, cbAuthTicket, steamID);
    }

    void EndAuthSession(CSteamID steamID) override
    {
        m_original->EndAuthSession(steamID);
    }

    void CancelAuthTicket(HAuthTicket hAuthTicket) override
    {
        if (s_clientGC)
        {
            s_clientGC->ClearAuthTicket(hAuthTicket);
        }

        m_original->CancelAuthTicket(hAuthTicket);
    }

    EUserHasLicenseForAppResult UserHasLicenseForApp(CSteamID steamID, AppId_t appID) override
    {
        return m_original->UserHasLicenseForApp(steamID, appID);
    }

    bool BIsBehindNAT() override
    {
        return m_original->BIsBehindNAT();
    }

    void AdvertiseGame(CSteamID steamIDGameServer, uint32 unIPServer, uint16 usPortServer) override
    {
        m_original->AdvertiseGame(steamIDGameServer, unIPServer, usPortServer);
    }

    SteamAPICall_t RequestEncryptedAppTicket(void *pDataToInclude, int cbDataToInclude) override
    {
        return m_original->RequestEncryptedAppTicket(pDataToInclude, cbDataToInclude);
    }

    bool GetEncryptedAppTicket(void *pTicket, int cbMaxTicket, uint32 *pcbTicket) override
    {
        return m_original->GetEncryptedAppTicket(pTicket, cbMaxTicket, pcbTicket);
    }

    int GetGameBadgeLevel(int nSeries, bool bFoil) override
    {
        return m_original->GetGameBadgeLevel(nSeries, bFoil);
    }

    int GetPlayerSteamLevel() override
    {
        return m_original->GetPlayerSteamLevel();
    }

    SteamAPICall_t RequestStoreAuthURL(const char *pchRedirectURL) override
    {
        return m_original->RequestStoreAuthURL(pchRedirectURL);
    }

    bool BIsPhoneVerified() override
    {
        return m_original->BIsPhoneVerified();
    }

    bool BIsTwoFactorEnabled() override
    {
        return m_original->BIsTwoFactorEnabled();
    }

    /*bool BIsPhoneIdentifying() override
    {
        return m_original->BIsPhoneIdentifying();
    }

    bool BIsPhoneRequiringVerification() override
    {
        return m_original->BIsPhoneRequiringVerification();
    }

    SteamAPICall_t GetMarketEligibility() override
    {
        return m_original->GetMarketEligibility();
    }

    SteamAPICall_t GetDurationControl() override
    {
        return m_original->GetDurationControl();
    }

    bool BSetDurationControlOnlineState(EDurationControlOnlineState eNewState) override
    {
        return m_original->BSetDurationControlOnlineState(eNewState);
    }*/
};

template<typename Interface, typename Proxy, typename... Args>
inline Interface *GetOrCreate(std::unique_ptr<Proxy> &pointer, Args &&... args)
{
    if (!pointer)
    {
        pointer = std::make_unique<Proxy>(std::forward<Args>(args)...);
    }

    return static_cast<Interface *>(pointer.get());
}

/* these are all the achievements obtainable in the game at this point:

Team Tactics (36):

MEDALIST - "Awardist"
WIN_BOMB_PLANT - "Someone Set Up Us The Bomb"
WIN_BOMB_DEFUSE - "Rite of First Defusal"
BOMB_DEFUSE_CLOSE_CALL - "Second to None"
KILL_BOMB_DEFUSER - "Counter-Counter-Terrorist"
BOMB_PLANT_IN_25_SECONDS - "Short Fuse"
KILL_BOMB_PICKUP - "Participation Award"
BOMB_MULTIKILL - "Clusterstruck"
GOOSE_CHASE - "Wild Gooseman Chase"
WIN_BOMB_PLANT_AFTER_RECOVERY - "Blast Will and Testament"
DEFUSE_DEFENSE - "Defusus Interruptus"
BOMB_PLANT_LOW - "Boomala Boomala"
BOMB_DEFUSE_LOW - "The Hurt Blocker"
RESCUE_ALL_HOSTAGES - "Good Shepherd"
KILL_HOSTAGE_RESCUER - "Dead Shepherd"
FAST_HOSTAGE_RESCUE - "Freed With Speed"
RESCUE_HOSTAGES_LOW - "Cowboy Diplomacy"
RESCUE_HOSTAGES_MED - "SAR Czar"
WIN_ROUNDS_LOW - "Newb World Order"
WIN_ROUNDS_MED - "Pro-moted"
WIN_ROUNDS_HIGH - "Leet-er of Men"
FAST_ROUND_WIN - "Blitzkrieg"
MERCY_RULE - "Mercy Rule"
FLAWLESS_VICTORY - "Clean Sweep"
KILLANTHROPIST - "Killanthropist"
BLOODLESS_VICTORY - "Cold War"
EARN_MONEY_LOW - "War Bonds"
EARN_MONEY_MED - "Spoils of War"
EARN_MONEY_HIGH - "Blood Money"
KILL_ENEMY_TEAM - "The Cleaner"
LAST_PLAYER_ALIVE - "War of Attrition"
WIN_PISTOLROUNDS_LOW - "Piece Initiative"
WIN_PISTOLROUNDS_MED - "Give Piece a Chance"
WIN_PISTOLROUNDS_HIGH - "Piece Treaty"
SILENT_WIN - "Black Bag Operation"
WIN_ROUNDS_WITHOUT_BUYING - "The Frugal Beret"

Combat Skills (40):

KILL_ENEMY_LOW - "Body Bagger"
KILL_ENEMY_MED - "Corpseman"
KILL_ENEMY_HIGH - "God of War"
KILL_ENEMY_RELOADING - "Shot With Their Pants Down"
KILLING_SPREE - "Ballistic"
KILLS_WITH_MULTIPLE_GUNS - "Variety Hour"
HEADSHOTS - "Battle Sight Zero"
SURVIVE_GRENADE - "Shrapnelproof"
KILL_ENEMY_BLINDED - "Blind Ambition"
KILL_ENEMIES_WHILE_BLIND - "Blind Fury"
KILL_ENEMIES_WHILE_BLIND_HARD - "Spray and Pray"
KILLS_ENEMY_WEAPON - "Friendly Firearms"
KILL_WITH_EVERY_WEAPON - "Expert Marksman"
WIN_KNIFE_FIGHTS_LOW - "Make the Cut"
WIN_KNIFE_FIGHTS_HIGH - "The Bleeding Edge"
KILLED_DEFUSER_WITH_GRENADE - "Defuse This!"
KILL_SNIPER_WITH_SNIPER - "Eye to Eye"
KILL_SNIPER_WITH_KNIFE - "Sknifed"
HIP_SHOT - "Hip Shot"
KILL_SNIPERS - "Snipe Hunter"
KILL_WHEN_AT_LOW_HEALTH - "Dead Man Stalking"
PISTOL_ROUND_KNIFE_KILL - "Street Fighter"
WIN_DUAL_DUEL - "Akimbo King"
GRENADE_MULTIKILL - "Three the Hard Way"
KILL_WHILE_IN_AIR - "Death From Above"
KILL_ENEMY_IN_AIR - "Bunny Hunt"
KILLER_AND_ENEMY_IN_AIR - "Aerial Necrobatics"
KILL_WITH_OWN_GUN - "Lost and F0wnd"
KILL_TWO_WITH_ONE_SHOT - "Ammo Conservation"
GIVE_DAMAGE_LOW - "Points in Your Favor"
GIVE_DAMAGE_MED - "You've Made Your Points"
GIVE_DAMAGE_HIGH - "A Million Points of Blight"
KILL_ENEMY_LAST_BULLET - "Magic Bullet"
KILLING_SPREE_ENDER - "Kill One, Get One Spree"
DAMAGE_NO_KILL - "Primer"
KILL_LOW_DAMAGE - "Finishing Schooled"
SURVIVE_MANY_ATTACKS - "Target-Hardened"
UNSTOPPABLE_FORCE - "The Unstoppable Force"
IMMOVABLE_OBJECT - "The Immovable Object"
HEADSHOTS_IN_ROUND - "Head Shred Redemption"

Weapon Specialist (39):

KILL_ENEMY_DEAGLE - "Desert Eagle Expert"
KILL_ENEMY_HKP2000 - "P2000/USP Tactical Expert"
KILL_ENEMY_GLOCK - "Glock-18 Expert"
KILL_ENEMY_P250 - "P250 Expert"
KILL_ENEMY_ELITE - "Dual Berettas Expert"
KILL_ENEMY_FIVESEVEN - "Five-SeveN Expert"
KILL_ENEMY_AWP - "AWP Expert"
KILL_ENEMY_AK47 - "AK-47 Expert"
KILL_ENEMY_M4A1 - "M4 AR Expert"
KILL_ENEMY_AUG - "AUG Expert"
KILL_ENEMY_SG556 - "SG553 Expert"
KILL_ENEMY_SCAR20 - "SCAR-20 Expert"
KILL_ENEMY_GALILAR - "Galil AR Expert"
KILL_ENEMY_FAMAS - "FAMAS Expert"
KILL_ENEMY_SSG08 - "SSG 08 Expert"
KILL_ENEMY_G3SG1 - "G3SG1 Expert"
KILL_ENEMY_P90 - "P90 Expert"
KILL_ENEMY_MP7 - "MP7 Expert"
KILL_ENEMY_MP9 - "MP9 Expert"
KILL_ENEMY_MAC10 - "MAC-10 Expert"
KILL_ENEMY_UMP45 - "UMP-45 Expert"
KILL_ENEMY_NOVA - "Nova Expert"
KILL_ENEMY_XM1014 - "XM1014 Expert"
KILL_ENEMY_MAG7 - "MAG-7 Expert"
KILL_ENEMY_M249 - "M249 Expert"
KILL_ENEMY_NEGEV - "Negev Expert"
KILL_ENEMY_TEC9 - "Tec-9 Expert"
KILL_ENEMY_SAWEDOFF - "Sawed-Off Expert"
KILL_ENEMY_BIZON - "PP-Bizon Expert"
KILL_ENEMY_KNIFE - "Knife Expert"
KILL_ENEMY_HEGRENADE - "HE Grenade Expert"
KILL_ENEMY_MOLOTOV - "Flame Expert"
DEAD_GRENADE_KILL - "Premature Burial"
META_PISTOL - "Pistol Master"
META_RIFLE - "Rifle Master"
META_SMG - "Sub-Machine Gun Master"
META_SHOTGUN - "Shotgun Master"
META_WEAPONMASTER - "Master At Arms"
KILL_ENEMY_TASER - "Zeus x27 Expert"

Global Expertise (17):

WIN_MAP_CS_ITALY - "Italy Map Veteran"
WIN_MAP_CS_OFFICE - "Office Map Veteran"
WIN_MAP_DE_AZTEC - "Aztec Map Veteran"
WIN_MAP_DE_DUST - "Dust Map Veteran"
WIN_MAP_DE_DUST2 - "Dust2 Map Veteran"
WIN_MAP_DE_INFERNO - "Inferno Map Veteran"
WIN_MAP_DE_NUKE - "Nuke Map Veteran"
WIN_MAP_DE_TRAIN - "Train Map Veteran"
WIN_MAP_AR_SHOOTS - "Shoots Vet"
WIN_MAP_AR_BAGGAGE - "Baggage Claimer"
WIN_MAP_DE_LAKE - "Vacation"
WIN_MAP_DE_SAFEHOUSE - "My House"
WIN_MAP_DE_SUGARCANE - "Run of the Mill"
WIN_MAP_DE_STMARC - "Marcsman"
WIN_MAP_DE_BANK - "Bank On It"
WIN_MAP_DE_SHORTTRAIN - "Shorttrain Map Veteran"
BREAK_WINDOWS - "A World of Pane"

Arms Race & Demolition (35):

PLAY_EVERY_GUNGAME_MAP - "Tourist"
GUN_GAME_KILL_KNIFER - "Denied!"
WIN_EVERY_GUNGAME_MAP - "Marksman"
GUN_GAME_RAMPAGE - "Rampage!"
GUN_GAME_FIRST_KILL - "FIRST!"
ONE_SHOT_ONE_KILL - "One Shot One Kill"
GUN_GAME_CONSERVATIONIST - "Conservationist"
TR_BOMB_PLANT_LOW - "Shorter Fuse"
TR_BOMB_DEFUSE_LOW - "Quick Cut"
GUN_GAME_FIRST_THING_FIRST - "First Things First"
GUN_GAME_TARGET_SECURED - "Target Secured"
BORN_READY - "Born Ready"
BASE_SCAMPER - "Base Scamper"
GUN_GAME_KNIFE_KILL_KNIFER - "Knife on Knife"
GUN_GAME_SMG_KILL_KNIFER - "Level Playing Field"
STILL_ALIVE - "Still Alive"
GUN_GAME_ROUNDS_LOW - "Practice Practice Practice"
GUN_GAME_ROUNDS_MED - "Gun Collector"
GUN_GAME_ROUNDS_HIGH - "King of the Kill"
WIN_GUN_GAME_ROUNDS_LOW - "Gungamer"
WIN_GUN_GAME_ROUNDS_MED - "Keep on Gunning"
WIN_GUN_GAME_ROUNDS_HIGH - "Kill of the Century"
WIN_GUN_GAME_ROUNDS_EXTREME - "The Professional"
WIN_GUN_GAME_ROUNDS_ULTIMATE - "Cold Pizza Eater"
DOMINATIONS_LOW - "Repeat Offender"
DOMINATIONS_HIGH - "Decimator"
REVENGES_LOW - "Insurgent"
REVENGES_HIGH - "Can't Keep a Good Man Down"
DOMINATION_OVERKILLS_LOW - "Overkill"
DOMINATION_OVERKILLS_HIGH - "Command and Control"
DOMINATION_OVERKILLS_MATCH - "Ten Angry Men"
EXTENDED_DOMINATION - "Excessive Brutality"
HAT_TRICK - "Hat Trick"
CAUSE_FRIENDLY_FIRE_WITH_FLASHBANG - "The Road to Hell"
AVENGE_FRIEND - "Avenging Angel"
*/

/*class SteamUserStatsProxy final : public ISteamUserStats
{
private:
    ISteamUserStats* m_original;

public:
    SteamUserStatsProxy(ISteamUserStats* original) : m_original(original) 
    {
    }

    bool RequestCurrentStats() override 
    {
        Platform::Print("SteamUserStatsProxy::RequestCurrentStats\n");
        return m_original->RequestCurrentStats();
    }

    bool GetStat(const char* pchName, int32* pData) override 
    {
        Platform::Print("SteamUserStatsProxy::GetStat(int) - %s\n", pchName);
        return m_original->GetStat(pchName, pData);
    }

    bool GetStat(const char* pchName, float* pData) override 
    {
        Platform::Print("SteamUserStatsProxy::GetStat(float) - %s\n", pchName);
        return m_original->GetStat(pchName, pData);
    }

    bool SetStat(const char* pchName, int32 nData) override 
    {
        Platform::Print("SteamUserStatsProxy::SetStat(int) - %s = %d\n", pchName, nData);
        return m_original->SetStat(pchName, nData);
    }

    bool SetStat(const char* pchName, float fData) override 
    {
        Platform::Print("SteamUserStatsProxy::SetStat(float) - %s = %f\n", pchName, fData);
        return m_original->SetStat(pchName, fData);
    }

    bool UpdateAvgRateStat(const char* pchName, float flCountThisSession, double dSessionLength) override 
    {
        Platform::Print("SteamUserStatsProxy::UpdateAvgRateStat - %s\n", pchName);
        return m_original->UpdateAvgRateStat(pchName, flCountThisSession, dSessionLength);
    }

    bool GetAchievement(const char* pchName, bool* pbAchieved) override 
    {
        Platform::Print("SteamUserStatsProxy::GetAchievement - %s\n", pchName);
        return m_original->GetAchievement(pchName, pbAchieved);
    }

    bool SetAchievement(const char* pchName) override 
    {
        Platform::Print("SteamUserStatsProxy::SetAchievement - %s\n", pchName);
        return m_original->SetAchievement(pchName);
    }

    bool ClearAchievement(const char* pchName) override 
    {
        Platform::Print("SteamUserStatsProxy::ClearAchievement - %s\n", pchName);
        return m_original->ClearAchievement(pchName);
    }

    bool GetAchievementAndUnlockTime(const char* pchName, bool* pbAchieved, uint32* punUnlockTime) override 
    {
        Platform::Print("SteamUserStatsProxy::GetAchievementAndUnlockTime - %s\n", pchName);
        return m_original->GetAchievementAndUnlockTime(pchName, pbAchieved, punUnlockTime);
    }

    bool StoreStats() override 
    {
        Platform::Print("SteamUserStatsProxy::StoreStats\n");
        return m_original->StoreStats();
    }

    int GetAchievementIcon(const char* pchName) override 
    {
        Platform::Print("SteamUserStatsProxy::GetAchievementIcon - %s\n", pchName);
        return m_original->GetAchievementIcon(pchName);
    }

    const char* GetAchievementDisplayAttribute(const char* pchName, const char* pchKey) override 
    {
        Platform::Print("SteamUserStatsProxy::GetAchievementDisplayAttribute - %s\n", pchName);
        return m_original->GetAchievementDisplayAttribute(pchName, pchKey);
    }

    bool IndicateAchievementProgress(const char* pchName, uint32 nCurProgress, uint32 nMaxProgress) override 
    {
        Platform::Print("SteamUserStatsProxy::IndicateAchievementProgress - %s (%u/%u)\n", pchName, nCurProgress, nMaxProgress);
        return m_original->IndicateAchievementProgress(pchName, nCurProgress, nMaxProgress);
    }

    uint32 GetNumAchievements() override 
    {
        Platform::Print("SteamUserStatsProxy::GetNumAchievements\n");
        return m_original->GetNumAchievements();
    }

    const char* GetAchievementName(uint32 iAchievement) override 
    {
        Platform::Print("SteamUserStatsProxy::GetAchievementName - %u\n", iAchievement);
        return m_original->GetAchievementName(iAchievement);
    }

    SteamAPICall_t RequestUserStats(CSteamID steamIDUser) override 
    {
        Platform::Print("SteamUserStatsProxy::RequestUserStats\n");
        return m_original->RequestUserStats(steamIDUser);
    }

    bool GetUserStat(CSteamID steamIDUser, const char* pchName, int32* pData) override 
    {
        Platform::Print("SteamUserStatsProxy::GetUserStat(int) - %s\n", pchName);
        return m_original->GetUserStat(steamIDUser, pchName, pData);
    }

    bool GetUserStat(CSteamID steamIDUser, const char* pchName, float* pData) override 
    {
        Platform::Print("SteamUserStatsProxy::GetUserStat(float) - %s\n", pchName);
        return m_original->GetUserStat(steamIDUser, pchName, pData);
    }

    bool GetUserAchievement(CSteamID steamIDUser, const char* pchName, bool* pbAchieved) override 
    {
        Platform::Print("SteamUserStatsProxy::GetUserAchievement - %s\n", pchName);
        return m_original->GetUserAchievement(steamIDUser, pchName, pbAchieved);
    }

    bool GetUserAchievementAndUnlockTime(CSteamID steamIDUser, const char* pchName, bool* pbAchieved, uint32* punUnlockTime) override 
    {
        Platform::Print("SteamUserStatsProxy::GetUserAchievementAndUnlockTime - %s\n", pchName);
        return m_original->GetUserAchievementAndUnlockTime(steamIDUser, pchName, pbAchieved, punUnlockTime);
    }

    bool ResetAllStats(bool bAchievementsToo) override 
    {
        Platform::Print("SteamUserStatsProxy::ResetAllStats - %s\n", bAchievementsToo ? "true" : "false");
        return m_original->ResetAllStats(bAchievementsToo);
    }

    SteamAPICall_t FindOrCreateLeaderboard(const char* pchLeaderboardName, ELeaderboardSortMethod eLeaderboardSortMethod, ELeaderboardDisplayType eLeaderboardDisplayType) override 
    {
        Platform::Print("SteamUserStatsProxy::FindOrCreateLeaderboard - %s\n", pchLeaderboardName);
        return m_original->FindOrCreateLeaderboard(pchLeaderboardName, eLeaderboardSortMethod, eLeaderboardDisplayType);
    }

    SteamAPICall_t FindLeaderboard(const char* pchLeaderboardName) override 
    {
        Platform::Print("SteamUserStatsProxy::FindLeaderboard - %s\n", pchLeaderboardName);
        return m_original->FindLeaderboard(pchLeaderboardName);
    }

    const char* GetLeaderboardName(SteamLeaderboard_t hSteamLeaderboard) override 
    {
        Platform::Print("SteamUserStatsProxy::GetLeaderboardName\n");
        return m_original->GetLeaderboardName(hSteamLeaderboard);
    }

    int GetLeaderboardEntryCount(SteamLeaderboard_t hSteamLeaderboard) override 
    {
        Platform::Print("SteamUserStatsProxy::GetLeaderboardEntryCount\n");
        return m_original->GetLeaderboardEntryCount(hSteamLeaderboard);
    }

    ELeaderboardSortMethod GetLeaderboardSortMethod(SteamLeaderboard_t hSteamLeaderboard) override 
    {
        Platform::Print("SteamUserStatsProxy::GetLeaderboardSortMethod\n");
        return m_original->GetLeaderboardSortMethod(hSteamLeaderboard);
    }

    ELeaderboardDisplayType GetLeaderboardDisplayType(SteamLeaderboard_t hSteamLeaderboard) override 
    {
        Platform::Print("SteamUserStatsProxy::GetLeaderboardDisplayType\n");
        return m_original->GetLeaderboardDisplayType(hSteamLeaderboard);
    }

    SteamAPICall_t DownloadLeaderboardEntries(SteamLeaderboard_t hSteamLeaderboard, ELeaderboardDataRequest eLeaderboardDataRequest, int nRangeStart, int nRangeEnd) override 
    {
        Platform::Print("SteamUserStatsProxy::DownloadLeaderboardEntries - Range %d-%d\n", nRangeStart, nRangeEnd);
        return m_original->DownloadLeaderboardEntries(hSteamLeaderboard, eLeaderboardDataRequest, nRangeStart, nRangeEnd);
    }

    SteamAPICall_t DownloadLeaderboardEntriesForUsers(SteamLeaderboard_t hSteamLeaderboard, CSteamID* prgUsers, int cUsers) override 
    {
        Platform::Print("SteamUserStatsProxy::DownloadLeaderboardEntriesForUsers - %d users\n", cUsers);
        return m_original->DownloadLeaderboardEntriesForUsers(hSteamLeaderboard, prgUsers, cUsers);
    }

    bool GetDownloadedLeaderboardEntry(SteamLeaderboardEntries_t hSteamLeaderboardEntries, int index, LeaderboardEntry_t* pLeaderboardEntry, int32* pDetails, int cDetailsMax) override 
    {
        Platform::Print("SteamUserStatsProxy::GetDownloadedLeaderboardEntry - Index %d\n", index);
        return m_original->GetDownloadedLeaderboardEntry(hSteamLeaderboardEntries, index, pLeaderboardEntry, pDetails, cDetailsMax);
    }

    SteamAPICall_t UploadLeaderboardScore(SteamLeaderboard_t hSteamLeaderboard, ELeaderboardUploadScoreMethod eLeaderboardUploadScoreMethod, int32 nScore, const int32* pScoreDetails, int cScoreDetailsCount) override 
    {
        Platform::Print("SteamUserStatsProxy::UploadLeaderboardScore - Score %d\n", nScore);
        return m_original->UploadLeaderboardScore(hSteamLeaderboard, eLeaderboardUploadScoreMethod, nScore, pScoreDetails, cScoreDetailsCount);
    }

    SteamAPICall_t AttachLeaderboardUGC(SteamLeaderboard_t hSteamLeaderboard, UGCHandle_t hUGC) override 
    {
        Platform::Print("SteamUserStatsProxy::AttachLeaderboardUGC\n");
        return m_original->AttachLeaderboardUGC(hSteamLeaderboard, hUGC);
    }

    SteamAPICall_t GetNumberOfCurrentPlayers() override 
    {
        Platform::Print("SteamUserStatsProxy::GetNumberOfCurrentPlayers\n");
        return m_original->GetNumberOfCurrentPlayers();
    }

    SteamAPICall_t RequestGlobalAchievementPercentages() override 
    {
        Platform::Print("SteamUserStatsProxy::RequestGlobalAchievementPercentages\n");
        return m_original->RequestGlobalAchievementPercentages();
    }

    int GetMostAchievedAchievementInfo(char* pchName, uint32 unNameBufLen, float* pflPercent, bool* pbAchieved) override 
    {
        Platform::Print("SteamUserStatsProxy::GetMostAchievedAchievementInfo\n");
        return m_original->GetMostAchievedAchievementInfo(pchName, unNameBufLen, pflPercent, pbAchieved);
    }

    int GetNextMostAchievedAchievementInfo(int iIteratorPrevious, char* pchName, uint32 unNameBufLen, float* pflPercent, bool* pbAchieved) override 
    {
        Platform::Print("SteamUserStatsProxy::GetNextMostAchievedAchievementInfo\n");
        return m_original->GetNextMostAchievedAchievementInfo(iIteratorPrevious, pchName, unNameBufLen, pflPercent, pbAchieved);
    }

    bool GetAchievementAchievedPercent(const char* pchName, float* pflPercent) override 
    {
        Platform::Print("SteamUserStatsProxy::GetAchievementAchievedPercent - %s\n", pchName);
        return m_original->GetAchievementAchievedPercent(pchName, pflPercent);
    }

    SteamAPICall_t RequestGlobalStats(int nHistoryDays) override 
    {
        Platform::Print("SteamUserStatsProxy::RequestGlobalStats - Days %d\n", nHistoryDays);
        return m_original->RequestGlobalStats(nHistoryDays);
    }

    bool GetGlobalStat(const char* pchStatName, int64* pData) override 
    {
        Platform::Print("SteamUserStatsProxy::GetGlobalStat(int64) - %s\n", pchStatName);
        return m_original->GetGlobalStat(pchStatName, pData);
    }

    bool GetGlobalStat(const char* pchStatName, double* pData) override 
    {
        Platform::Print("SteamUserStatsProxy::GetGlobalStat(double) - %s\n", pchStatName);
        return m_original->GetGlobalStat(pchStatName, pData);
    }

    int32 GetGlobalStatHistory(const char* pchStatName, int64* pData, uint32 cubData) override 
    {
        Platform::Print("SteamUserStatsProxy::GetGlobalStatHistory(int64) - %s\n", pchStatName);
        return m_original->GetGlobalStatHistory(pchStatName, pData, cubData);
    }

    int32 GetGlobalStatHistory(const char* pchStatName, double* pData, uint32 cubData) override 
    {
        Platform::Print("SteamUserStatsProxy::GetGlobalStatHistory(double) - %s\n", pchStatName);
        return m_original->GetGlobalStatHistory(pchStatName, pData, cubData);
    }
};*/

/*class SteamMatchmakingServersProxy final : public ISteamMatchmakingServers
{
private:
    ISteamMatchmakingServers* m_original;

public:
    SteamMatchmakingServersProxy(ISteamMatchmakingServers* original)
        : m_original{ original }
    {
    }

    HServerListRequest RequestInternetServerList(AppId_t iApp, ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse* pRequestServersResponse) override
    {
        Platform::Print("RequestInternetServerList called with app %u\n", iApp);
        return m_original->RequestInternetServerList(iApp, ppchFilters, nFilters, pRequestServersResponse);
    }

    HServerListRequest RequestLANServerList(AppId_t iApp, ISteamMatchmakingServerListResponse* pRequestServersResponse) override
    {
        return m_original->RequestLANServerList(iApp, pRequestServersResponse);
    }

    HServerListRequest RequestFriendsServerList(AppId_t iApp, ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse* pRequestServersResponse) override
    {
        return m_original->RequestFriendsServerList(iApp, ppchFilters, nFilters, pRequestServersResponse);
    }

    HServerListRequest RequestFavoritesServerList(AppId_t iApp, ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse* pRequestServersResponse) override
    {
        return m_original->RequestFavoritesServerList(iApp, ppchFilters, nFilters, pRequestServersResponse);
    }

    HServerListRequest RequestHistoryServerList(AppId_t iApp, ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse* pRequestServersResponse) override
    {
        return m_original->RequestHistoryServerList(iApp, ppchFilters, nFilters, pRequestServersResponse);
    }

    HServerListRequest RequestSpectatorServerList(AppId_t iApp, ARRAY_COUNT(nFilters) MatchMakingKeyValuePair_t** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse* pRequestServersResponse) override
    {
        return m_original->RequestSpectatorServerList(iApp, ppchFilters, nFilters, pRequestServersResponse);
    }

    void ReleaseRequest(HServerListRequest hServerListRequest) override
    {
        m_original->ReleaseRequest(hServerListRequest);
    }

    gameserveritem_t* GetServerDetails(HServerListRequest hRequest, int iServer) override
    {
        Platform::Print("GetServerDetails called for server %d\n", iServer);
        return m_original->GetServerDetails(hRequest, iServer);
    }

    void CancelQuery(HServerListRequest hRequest) override
    {
        m_original->CancelQuery(hRequest);
    }

    void RefreshQuery(HServerListRequest hRequest) override
    {
        m_original->RefreshQuery(hRequest);
    }

    bool IsRefreshing(HServerListRequest hRequest) override
    {
        return m_original->IsRefreshing(hRequest);
    }

    int GetServerCount(HServerListRequest hRequest) override
    {
        int count = m_original->GetServerCount(hRequest);
        Platform::Print("GetServerCount returning %d servers\n", count);
        return count;
    }

    void RefreshServer(HServerListRequest hRequest, int iServer) override
    {
        m_original->RefreshServer(hRequest, iServer);
    }

    HServerQuery PingServer(uint32 unIP, uint16 usPort, ISteamMatchmakingPingResponse* pRequestServersResponse) override
    {
        return m_original->PingServer(unIP, usPort, pRequestServersResponse);
    }

    HServerQuery PlayerDetails(uint32 unIP, uint16 usPort, ISteamMatchmakingPlayersResponse* pRequestServersResponse) override
    {
        return m_original->PlayerDetails(unIP, usPort, pRequestServersResponse);
    }

    HServerQuery ServerRules(uint32 unIP, uint16 usPort, ISteamMatchmakingRulesResponse* pRequestServersResponse) override
    {
        return m_original->ServerRules(unIP, usPort, pRequestServersResponse);
    }

    void CancelServerQuery(HServerQuery hServerQuery) override
    {
        m_original->CancelServerQuery(hServerQuery);
    }
};*/



class SteamInterfaceProxy
{
public:
    SteamInterfaceProxy(HSteamPipe pipe, HSteamUser user)
        : m_pipe{ pipe }
        , m_user{ user }
    {
    }

    void *GetInterface(const char *version, void *original)
    {
        if (InterfaceMatches(version, STEAMGAMECOORDINATOR_INTERFACE_VERSION))
        {
            // pass 0 as steamid for servers so the wrapper knows it's for a server
            uint64_t steamId = 0;

            if (SteamGameServer_GetHSteamPipe() != m_pipe)
            {
                steamId = SteamUser()->GetSteamID().ConvertToUint64();
            }

            return GetOrCreate<ISteamGameCoordinator>(m_steamGameCoordinator, steamId);
        }
        else if (InterfaceMatches(version, STEAMUTILS_INTERFACE_VERSION))
        {
            return GetOrCreate<ISteamUtils>(m_steamUtils, static_cast<ISteamUtils *>(original));
        }
        else if (InterfaceMatches(version, STEAMGAMESERVER_INTERFACE_VERSION))
        {
            return GetOrCreate<ISteamGameServer>(m_steamGameServer, static_cast<ISteamGameServer *>(original));
        }
        else if (InterfaceMatches(version, STEAMUSER_INTERFACE_VERSION))
        {
            return GetOrCreate<ISteamUser>(m_steamUser, static_cast<ISteamUser *>(original));
        }
        else if (InterfaceMatches(version, STEAMFRIENDS_INTERFACE_VERSION))
        {
            return GetOrCreate<ISteamFriends>(m_steamFriends, static_cast<ISteamFriends*>(original));
        }
        else if (InterfaceMatches(version, STEAMINVENTORY_INTERFACE_VERSION))
        {
            return GetOrCreate<ISteamInventory>(m_steamInventory, static_cast<ISteamInventory*>(original));
        }
        /*else if (InterfaceMatches(version, STEAMUSERSTATS_INTERFACE_VERSION))
        {
            return GetOrCreate<ISteamUserStats>(m_steamUserStats, static_cast<ISteamUserStats*>(original));
        }*/
        /*else if (InterfaceMatches(version, STEAMMATCHMAKINGSERVERS_INTERFACE_VERSION))
        {
            Platform::Print("Creating SteamMatchmakingServersProxy for version %s\n", version);
            return GetOrCreate<ISteamMatchmakingServers>(m_steamMatchmakingServers, static_cast<ISteamMatchmakingServers*>(original));
        }*/

        return nullptr;
    }

private:
    const HSteamPipe m_pipe;
    const HSteamUser m_user;

    std::unique_ptr<SteamGameCoordinatorProxy> m_steamGameCoordinator;
    std::unique_ptr<SteamUtilsProxy> m_steamUtils;
    std::unique_ptr<SteamGameServerProxy> m_steamGameServer;
    std::unique_ptr<SteamUserProxy> m_steamUser;
    std::unique_ptr<SteamFriendsProxy> m_steamFriends;
    std::unique_ptr<SteamInventoryProxy> m_steamInventory;
    //std::unique_ptr<SteamUserStatsProxy> m_steamUserStats;
    //std::unique_ptr<SteamMatchmakingServersProxy> m_steamMatchmakingServers;
};

class SteamClientProxy : public ISteamClient
{
    ISteamClient *m_original{};
    std::unordered_map<uint64_t, SteamInterfaceProxy> m_proxies;

    uint64_t ProxyKey(HSteamPipe pipe, HSteamUser user)
    {
        return static_cast<uint64_t>(pipe) | (static_cast<uint64_t>(user) << 32);
    }

    SteamInterfaceProxy &GetProxy(HSteamPipe pipe, HSteamUser user, [[maybe_unused]] bool allowNoUser)
    {
        assert(pipe);
        assert(user || allowNoUser);

        auto result = m_proxies.try_emplace(ProxyKey(pipe, user), pipe, user);
        return result.first->second;
    }

    void RemoveProxy(HSteamPipe pipe, HSteamUser user)
    {
        auto it = m_proxies.find(ProxyKey(pipe, user));
        if (it != m_proxies.end())
        {
            m_proxies.erase(it);
        }
        else
        {
            assert(false);
        }
    }

protected:
    // The actual implementation must be protected to match base class
    void RunFrame() override {} // Empty implementation since it's deprecated

    void DEPRECATED_Set_SteamAPI_CPostAPIResultInProcess(void(*)()) override {}
    void DEPRECATED_Remove_SteamAPI_CPostAPIResultInProcess(void(*)()) override {}
    void Set_SteamAPI_CCheckCallbackRegisteredInProcess(SteamAPI_CheckCallbackRegistered_t func) override {}

public:
    void SetOriginal(ISteamClient *original)
    {
        assert(!m_original || m_original == original);
        m_original = original;
    }

    ~SteamClientProxy()
    {
        // debug schizo
        assert(m_proxies.empty());
    }

    HSteamPipe CreateSteamPipe() override
    {
        return m_original->CreateSteamPipe();
    }

    bool BReleaseSteamPipe(HSteamPipe hSteamPipe) override
    {
        // remove proxies not tied to a specific user, e.g. ISteamUtils
        RemoveProxy(hSteamPipe, 0);

        return m_original->BReleaseSteamPipe(hSteamPipe);
    }

    HSteamUser ConnectToGlobalUser(HSteamPipe hSteamPipe) override
    {
        return m_original->ConnectToGlobalUser(hSteamPipe);
    }

    HSteamUser CreateLocalUser(HSteamPipe *phSteamPipe, EAccountType eAccountType) override
    {
        return m_original->CreateLocalUser(phSteamPipe, eAccountType);
    }

    void ReleaseUser(HSteamPipe hSteamPipe, HSteamUser hUser) override
    {
        RemoveProxy(hSteamPipe, hUser);

        m_original->ReleaseUser(hSteamPipe, hUser);
    }

    template<typename T>
    T *ProxyInterface(T *original, HSteamUser user, HSteamPipe pipe, const char *version, bool allowNoUser = false)
    {
        SteamInterfaceProxy &proxy = GetProxy(pipe, user, allowNoUser);
        T *result = static_cast<T *>(proxy.GetInterface(version, original));
        return result ? result : original;
    }

    // temp macro
#define PROXY_INTERFACE(func, user, pipe, version, ...) ProxyInterface(m_original->func(user, pipe, version), user, pipe, version, ## __VA_ARGS__)

    ISteamUser *GetISteamUser(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamUser, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamGameServer *GetISteamGameServer(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamGameServer, hSteamUser, hSteamPipe, pchVersion);
    }

    void SetLocalIPBinding(uint32 unIP, uint16 usPort) override
    {
        m_original->SetLocalIPBinding(unIP, usPort);
    }

    ISteamFriends *GetISteamFriends(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamFriends, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamUtils *GetISteamUtils(HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return ProxyInterface(m_original->GetISteamUtils(hSteamPipe, pchVersion), 0, hSteamPipe, pchVersion, true);
    }

    ISteamMatchmaking *GetISteamMatchmaking(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamMatchmaking, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamMatchmakingServers *GetISteamMatchmakingServers(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamMatchmakingServers, hSteamUser, hSteamPipe, pchVersion);
    }

    void *GetISteamGenericInterface(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamGenericInterface, hSteamUser, hSteamPipe, pchVersion, true);
    }

    ISteamUserStats *GetISteamUserStats(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamUserStats, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamGameServerStats *GetISteamGameServerStats(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamGameServerStats, hSteamuser, hSteamPipe, pchVersion);
    }

    ISteamApps *GetISteamApps(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamApps, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamNetworking *GetISteamNetworking(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamNetworking, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamRemoteStorage *GetISteamRemoteStorage(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamRemoteStorage, hSteamuser, hSteamPipe, pchVersion);
    }

    ISteamScreenshots *GetISteamScreenshots(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamScreenshots, hSteamuser, hSteamPipe, pchVersion);
    }

    /*ISteamGameSearch* GetISteamGameSearch(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char* pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamGameSearch, hSteamuser, hSteamPipe, pchVersion);
    }*/

    /*void RunFrame() override
    {
        m_original->RunFrame();
    }*/

    uint32 GetIPCCallCount() override
    {
        return m_original->GetIPCCallCount();
    }

    void SetWarningMessageHook(SteamAPIWarningMessageHook_t pFunction) override
    {
        m_original->SetWarningMessageHook(pFunction);
    }

    bool BShutdownIfAllPipesClosed() override
    {
        return m_original->BShutdownIfAllPipesClosed();
    }

    ISteamHTTP *GetISteamHTTP(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamHTTP, hSteamuser, hSteamPipe, pchVersion);
    }

    ISteamUnifiedMessages* GetISteamUnifiedMessages(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char* pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamUnifiedMessages, hSteamuser, hSteamPipe, pchVersion);
    }

    ISteamController *GetISteamController(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamController, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamUGC *GetISteamUGC(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamUGC, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamAppList *GetISteamAppList(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamAppList, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamMusic *GetISteamMusic(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamMusic, hSteamuser, hSteamPipe, pchVersion);
    }

    ISteamMusicRemote *GetISteamMusicRemote(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamMusicRemote, hSteamuser, hSteamPipe, pchVersion);
    }

    ISteamHTMLSurface *GetISteamHTMLSurface(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamHTMLSurface, hSteamuser, hSteamPipe, pchVersion);
    }

    /*void DEPRECATED_Set_SteamAPI_CPostAPIResultInProcess(void(*func)()) override
    {
        m_original->DEPRECATED_Set_SteamAPI_CPostAPIResultInProcess(func);
    }

    void DEPRECATED_Remove_SteamAPI_CPostAPIResultInProcess(void(*func)()) override
    {
        m_original->DEPRECATED_Remove_SteamAPI_CPostAPIResultInProcess(func);
    }

    void Set_SteamAPI_CCheckCallbackRegisteredInProcess(SteamAPI_CheckCallbackRegistered_t func) override
    {
        m_original->Set_SteamAPI_CCheckCallbackRegisteredInProcess(func);
    }*/

    ISteamInventory *GetISteamInventory(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamInventory, hSteamuser, hSteamPipe, pchVersion);
    }

    ISteamVideo *GetISteamVideo(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamVideo, hSteamuser, hSteamPipe, pchVersion);
    }

    /*ISteamParentalSettings* GetISteamParentalSettings(HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char* pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamParentalSettings, hSteamuser, hSteamPipe, pchVersion);
    }

    ISteamInput *GetISteamInput(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamInput, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamParties *GetISteamParties(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamParties, hSteamUser, hSteamPipe, pchVersion);
    }

    ISteamRemotePlay *GetISteamRemotePlay(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion) override
    {
        return PROXY_INTERFACE(GetISteamRemotePlay, hSteamUser, hSteamPipe, pchVersion);
    }*/

    /*void DestroyAllInterfaces() override
    {
        m_proxies.clear();
        m_original->DestroyAllInterfaces();
    }*/
};

static SteamClientProxy s_steamClientProxy;

static void *(*Og_CreateInterface)(const char *, int *errorCode);

static void* Hk_CreateInterface(const char* name, int* errorCode)
{
    void* result = Og_CreateInterface(name, errorCode);

    if (InterfaceMatches(name, STEAMCLIENT_INTERFACE_VERSION))
    {
        s_steamClientProxy.SetOriginal(static_cast<ISteamClient*>(result));
        return &s_steamClientProxy;
    }
    
    return result;
}

struct CallbackHook
{
    int id;
    CCallbackBase *callback;
};

static bool ShouldHookCallback(int id)
{
    // we want to spoof all gc callbacks
    switch (id)
    {
    case GCMessageAvailable_t::k_iCallback:
    case GCMessageFailed_t::k_iCallback:
        return true;

    default:
        return false;
    }
}

class CallbackAccessor : public CCallbackBase
{
public:
    bool IsGameServer()
    {
        return m_nCallbackFlags & CCallbackBase::k_ECallbackFlagsGameServer;
    }
};

class CallbackHooks
{
public:
    // returns true if callback was spoofed
    bool RegisterCallback(CCallbackBase *callback, int id)
    {
        if (!ShouldHookCallback(id))
        {
            return false;
        }

        CallbackHook callbackHook{ id, callback };
        m_hooks.push_back(callbackHook);

        return true;
    }

    // returns true if callback was spoofed
    bool UnregisterCallback(CCallbackBase *callback)
    {
        if (!ShouldHookCallback(callback->GetICallback()))
        {
            return false;
        }

        auto remove = [callback](const CallbackHook &hook) {
            return (hook.callback == callback);
        };

        m_hooks.erase(std::remove_if(m_hooks.begin(), m_hooks.end(), remove), m_hooks.end());

        return true;
    }

    // runs callbacks matching id immediately
    void RunCallback(bool server, int id, void *param)
    {
        for (const CallbackHook &hook : m_hooks)
        {
            bool serverCallback = static_cast<CallbackAccessor *>(hook.callback)->IsGameServer();
            if (server == serverCallback && hook.id == id)
            {
                hook.callback->Run(param);
            }
        }
    }

private:
    std::vector<CallbackHook> m_hooks;
};

static CallbackHooks s_callbackHooks;

static void (*Og_SteamAPI_RegisterCallback)(class CCallbackBase *pCallback, int iCallback);
static void (*Og_SteamAPI_UnregisterCallback)(class CCallbackBase *pCallback);
static void (*Og_SteamAPI_RunCallbacks)();
static void (*Og_SteamGameServer_RunCallbacks)();

static void Hk_SteamAPI_RegisterCallback(class CCallbackBase *pCallback, int iCallback)
{
    if (s_callbackHooks.RegisterCallback(pCallback, iCallback))
    {
        return;
    }

    Og_SteamAPI_RegisterCallback(pCallback, iCallback);
}

static void Hk_SteamAPI_UnregisterCallback(class CCallbackBase *pCallback)
{
    if (s_callbackHooks.UnregisterCallback(pCallback))
    {
        return;
    }

    Og_SteamAPI_UnregisterCallback(pCallback);
}

static void Hk_SteamAPI_RunCallbacks()
{
    Og_SteamAPI_RunCallbacks();

    if (s_clientGC)
    {
        // run client gc callbacks
        uint32_t messageSize;
        if (s_clientGC->HasOutgoingMessages(messageSize))
        {
            GCMessageAvailable_t param{};
            param.m_nMessageSize = messageSize;
            s_callbackHooks.RunCallback(false, GCMessageAvailable_t::k_iCallback, &param);
        }

        // do networking stuff
        s_clientGC->Update();
    }
}

static void Hk_SteamGameServer_RunCallbacks()
{
    Og_SteamGameServer_RunCallbacks();

    if (s_serverGC)
    {
        // run server gc callbacks
        uint32_t messageSize;
        if (s_serverGC->HasOutgoingMessages(messageSize))
        {
            GCMessageAvailable_t param{};
            param.m_nMessageSize = messageSize;
            s_callbackHooks.RunCallback(true, GCMessageAvailable_t::k_iCallback, &param);
        }

        // do networking stuff
        s_serverGC->Update();
    }
}

// shows a message box and exits on failure
static void HookCreate(const char *name, void *target, void *hook, void **bridge)
{
    funchook_t *funchook = funchook_create();
    if (!funchook)
    {
        // unlikely (only allocates) but check anyway
        Platform::Error("funchook_create failed for %s", name);
    }

    void *temp = target;
    int result = funchook_prepare(funchook, &temp, hook);
    if (result != 0)
    {
        Platform::Error("funchook_prepare failed for %s: %s", name, funchook_error_message(funchook));
    }

    *bridge = temp;

    result = funchook_install(funchook, 0);
    if (result != 0)
    {
        Platform::Error("funchook_install failed for %s: %s", name, funchook_error_message(funchook));
    }
}

#define INLINE_HOOK(a) HookCreate(#a, reinterpret_cast<void *>(a), reinterpret_cast<void *>(Hk_##a), reinterpret_cast<void **>(&Og_##a));

static bool InitializeSteamAPI(bool dedicated)
{
    if (dedicated)
    {
        return SteamGameServer_Init(0, 0, 0, MASTERSERVERUPDATERPORT_USEGAMESOCKETSHARE, eServerModeNoAuthentication, "1.38.7.9");
    }
    else
    {
        return SteamAPI_Init();
    }
}

static void ShutdownSteamAPI(bool dedicated)
{
    if (dedicated)
    {
        SteamGameServer_Shutdown();
    }
    else
    {
        SteamAPI_Shutdown();
    }
}


void SteamHookInstall(bool dedicated)
{
    Platform::EnsureEnvVarSet("SteamAppId", "730");

    // this is bit of a clusterfuck
    if (!InitializeSteamAPI(dedicated))
    {
        Platform::Error("Steam initialization failed. Please try the following steps:\n"
            "- Ensure that Steam is running.\n"
            "- Restart Steam and try again.\n"
            "- Verify that you have launched CS:GO or CS2 through Steam at least once.");
    }

    uint8_t steamClientPath[4096]; // NOTE: text encoding stored depends on the platform (wchar_t on windows)
    if (!Platform::SteamClientPath(steamClientPath, sizeof(steamClientPath)))
    {
        Platform::Error("Could not get steamclient module path");
    }

    // decrement reference count
    ShutdownSteamAPI(dedicated);

    // load steamclient
    void *CreateInterface = Platform::SteamClientFactory(steamClientPath);
    if (!CreateInterface)
    {
        Platform::Error("Could not get steamclient factory");
    }

    INLINE_HOOK(CreateInterface);

    // steam api hooks for gc callbacks
    INLINE_HOOK(SteamAPI_RegisterCallback);
    INLINE_HOOK(SteamAPI_UnregisterCallback);
    INLINE_HOOK(SteamAPI_RunCallbacks);
    INLINE_HOOK(SteamGameServer_RunCallbacks);
}
