#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/UpnpAv/UpnpAv.h>
#include <OpenHome/Configuration/Tests/ConfigRamStore.h>
#include <OpenHome/Av/Utils/IconDriverSongcastSender.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Private/Printer.h>

#include <Windows.h>
#include "ExampleMediaPlayer.h"
#include "RamStore.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

// ExampleMediaPlayer

const Brn ExampleMediaPlayer::kSongcastSenderIconFileName("SongcastSenderIcon");

ExampleMediaPlayer::ExampleMediaPlayer(Net::DvStack& aDvStack,
                                       const Brx& aUdn,
                                       const TChar* aRoom,
                                       const TChar* aProductName,
                                       const Brx& aUserAgent)
    : iSemShutdown("TMPS", 0)
    , iDisabled("test", 0)
    , iUserAgent(aUserAgent)
{
    Bws<256> friendlyName;
    friendlyName.Append(aRoom);
    friendlyName.Append(':');
    friendlyName.Append(aProductName);

    // create UPnP device
    iDevice = new DvDeviceStandard(aDvStack, aUdn, *this);
    iDevice->SetAttribute("Upnp.Domain", "av.openhome.org");
    iDevice->SetAttribute("Upnp.Type", "Source");
    iDevice->SetAttribute("Upnp.Version", "1");
    iDevice->SetAttribute("Upnp.FriendlyName", friendlyName.PtrZ());
    iDevice->SetAttribute("Upnp.Manufacturer", "OpenHome");
    iDevice->SetAttribute("Upnp.ModelName", "ExampleMediaPlayer");

    // create separate UPnP device for standard MediaRenderer
    Bws<256> buf(aUdn);
    buf.Append("-MediaRenderer");

    // The renderer name should be <room name>:<UPnP AV source name> to allow
    // our control point to match the renderer device to the upnp av source.
    Bws<256> rendererName(aRoom);
    rendererName.Append(":");
    rendererName.Append(SourceUpnpAv::kSourceName);
    iDeviceUpnpAv = new DvDeviceStandard(aDvStack, buf);
    iDeviceUpnpAv->SetAttribute("Upnp.Domain", "upnp.org");
    iDeviceUpnpAv->SetAttribute("Upnp.Type", "MediaRenderer");
    iDeviceUpnpAv->SetAttribute("Upnp.Version", "1");
    friendlyName.Append(":MediaRenderer");
    iDeviceUpnpAv->SetAttribute("Upnp.FriendlyName", rendererName.PtrZ());
    iDeviceUpnpAv->SetAttribute("Upnp.Manufacturer", "OpenHome");
    iDeviceUpnpAv->SetAttribute("Upnp.ModelName", "ExampleMediaPlayer");

    // create read/write store.  This creates a number of static (constant)
    // entries automatically
    // FIXME - to be removed; this only exists to populate static data
    iRamStore = new RamStore();

    // create a read/write store using the new config framework
    iConfigRamStore = new ConfigRamStore();

    // FIXME - available store keys should be listed somewhere
    iConfigRamStore->Write(Brn("Product.Room"), Brn(aRoom));
    iConfigRamStore->Write(Brn("Product.Name"), Brn(aProductName));

    // ALDO - Set pipepline htread priority just below the pipeline animator.
    iInitParams = PipelineInitParams::New();
    iInitParams->SetThreadPriorityMax(kPriorityHighest);
    iInitParams->SetStarvationMonitorMaxSize(Jiffies::kPerMs * 50 *2);

    // create MediaPlayer
    iMediaPlayer = new MediaPlayer( aDvStack, *iDevice, *iRamStore,
                                   *iConfigRamStore, iInitParams,
                                    iVolume, iVolume, aUdn, Brn(aRoom),
                                    Brn("Softplayer"));

    // Register an observer, primarily to monitor the pipeline status.
    iMediaPlayer->Pipeline().AddObserver(*this);
}

ExampleMediaPlayer::~ExampleMediaPlayer()
{
    ASSERT(!iDevice->Enabled());
    delete iMediaPlayer;
    delete iDevice;
    delete iDeviceUpnpAv;
    delete iRamStore;
    delete iConfigRamStore;
}

void ExampleMediaPlayer::StopPipeline()
{
    TUint waitCount = 0;
    if (TryDisable(*iDevice)) {
        waitCount++;
    }
    if (TryDisable(*iDeviceUpnpAv)) {
        waitCount++;
    }
    while (waitCount > 0) {
        iDisabled.Wait();
        waitCount--;
    }
    iMediaPlayer->Quit();

    iSemShutdown.Signal();
}

void ExampleMediaPlayer::PlayPipeline()
{
    if (pState != EPipelinePlaying)
    {
        iMediaPlayer->Pipeline().Play();
    }
}

void ExampleMediaPlayer::PausePipeline()
{
    if (pState == EPipelinePlaying)
    {
        iMediaPlayer->Pipeline().Pause();
    }
}

void ExampleMediaPlayer::HaltPipeline()
{
    if (pState == EPipelinePlaying || pState == EPipelinePaused)
    {
        iMediaPlayer->Pipeline().Stop();
    }
}

void ExampleMediaPlayer::AddAttribute(const TChar* aAttribute)
{
    iMediaPlayer->AddAttribute(aAttribute);
}

void ExampleMediaPlayer::RunWithSemaphore()
{
    RegisterPlugins(iMediaPlayer->Env());
    iMediaPlayer->Start();
    iDevice->SetEnabled();
    iDeviceUpnpAv->SetEnabled();

    iSemShutdown.Wait();
}

PipelineManager& ExampleMediaPlayer::Pipeline()
{
    return iMediaPlayer->Pipeline();
}

DvDeviceStandard* ExampleMediaPlayer::Device()
{
    return iDevice;
}

void ExampleMediaPlayer::RegisterPlugins(Environment& aEnv)
{
    const Brn kSupportedProtocols(
        "http-get:*:audio/x-flac:*,"    // Flac
        "http-get:*:audio/wav:*,"       // Wav
        "http-get:*:audio/wave:*,"      // Wav
        "http-get:*:audio/x-wav:*,"     // Wav
        "http-get:*:audio/aiff:*,"      // AIFF
        "http-get:*:audio/x-aiff:*,"    // AIFF
        "http-get:*:audio/x-m4a:*,"     // Alac
        "http-get:*:audio/x-scpls:*,"   // M3u (content processor)
        "http-get:*:text/xml:*,"        // Opml ??  (content processor)
        "http-get:*:audio/aac:*,"       // Aac
        "http-get:*:audio/aacp:*,"      // Aac
        "http-get:*:audio/mp4:*,"       // Mpeg4 (container)
        "http-get:*:audio/ogg:*,"       // Vorbis
        "http-get:*:audio/x-ogg:*,"     // Vorbis
        "http-get:*:application/ogg:*," // Vorbis
        );
    DoRegisterPlugins(aEnv, kSupportedProtocols);
}

void ExampleMediaPlayer::DoRegisterPlugins(Environment& aEnv, const Brx& aSupportedProtocols)
{
    // Add codecs
    Log::Print("Condec Registration: [\n");

    Log::Print("Codec\tAac\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAac());
    Log::Print("Codec\tAiff\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAiff());
    Log::Print("Codec\tAifc\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAifc());
    Log::Print("Codec\tAlac\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAlac());
    Log::Print("Codec\tAdts\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAdts());
    Log::Print("Codec:\tFlac\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewFlac());
    Log::Print("Codec\tPcm\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewPcm());
    Log::Print("Codec\tVorbis\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewVorbis());
    Log::Print("Codec\tWav\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewWav());

    Log::Print("]\n");

    // Add protocol modules
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));

    // Add sources
    iMediaPlayer->Add(SourceFactory::NewPlaylist(*iMediaPlayer,
                                                 aSupportedProtocols));

    iMediaPlayer->Add(SourceFactory::NewUpnpAv(*iMediaPlayer,
                                               *iDeviceUpnpAv,
                                                aSupportedProtocols));

    iMediaPlayer->Add(SourceFactory::NewReceiver(*iMediaPlayer,
                                                  NULL,
                                                  NULL,
                                                  kSongcastSenderIconFileName));
}

void ExampleMediaPlayer::WriteResource(const Brx& aUriTail,
                                       TIpAddress /*aInterface*/,
                                       std::vector<char*>& /*aLanguageList*/,
                                       IResourceWriter& aResourceWriter)
{
    if (aUriTail == kSongcastSenderIconFileName) {
        aResourceWriter.WriteResourceBegin(sizeof(kIconDriverSongcastSender),
                                           kIconDriverSongcastSenderMimeType);
        aResourceWriter.WriteResource(kIconDriverSongcastSender,
                                      sizeof(kIconDriverSongcastSender));
        aResourceWriter.WriteResourceEnd();
    }
}

TUint ExampleMediaPlayer::Hash(const Brx& aBuf)
{
    TUint hash = 0;
    for (TUint i=0; i<aBuf.Bytes(); i++) {
        hash += aBuf[i];
    }
    return hash;
}

TBool ExampleMediaPlayer::TryDisable(DvDevice& aDevice)
{
    if (aDevice.Enabled()) {
        aDevice.SetDisabled(MakeFunctor(*this, &ExampleMediaPlayer::Disabled));
        return true;
    }
    return false;
}

void ExampleMediaPlayer::Disabled()
{
    iDisabled.Signal();
}

// ExampleMediaPlayerInit

OpenHome::Net::Library* ExampleMediaPlayerInit::CreateLibrary()
{
    InitialisationParams* initParams = InitialisationParams::Create();
    initParams->SetDvEnableBonjour();

    Net::Library* lib = new Net::Library(initParams);

    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();

    if (subnetList->size() == 0) {
        Log::Print("ERROR: No adapters found\n");
        ASSERTS();
    }

    Log::Print ("Adapter List:\n");
    for (unsigned i=0; i<subnetList->size(); ++i) {
        TIpAddress addr = (*subnetList)[i]->Address();
        Log::Print ("  %d: %d.%d.%d.%d\n", i, addr&0xff, (addr>>8)&0xff,
                                           (addr>>16)&0xff, (addr>>24)&0xff);
    }

    // Choose the first adapter.
    //TIpAddress subnet = (*subnetList)[3]->Subnet();
    TIpAddress subnet = (*subnetList)[0]->Subnet();
    Library::DestroySubnetList(subnetList);
    lib->SetCurrentSubnet(subnet);

    Log::Print("Using Subnet %d.%d.%d.%d\n", subnet&0xff, (subnet>>8)&0xff,
                                             (subnet>>16)&0xff,
                                             (subnet>>24)&0xff);

    return lib;
}

// Pipeline Observer callbacks.
void ExampleMediaPlayer::NotifyPipelineState(Media::EPipelineState aState)
{
    pState = aState;

    //Log::Print("Pipeline State: %d\n", aState);
}

void ExampleMediaPlayer::NotifyTrack(Media::Track& /*aTrack*/, const Brx& /*aMode*/, TBool /*aStartOfStream*/)
{
}

void ExampleMediaPlayer::NotifyMetaText(const Brx& /*aText*/)
{
}

void ExampleMediaPlayer::NotifyTime(TUint /*aSeconds*/, TUint /*aTrackDurationSeconds*/)
{
}

void ExampleMediaPlayer::NotifyStreamInfo(const Media::DecodedStreamInfo& /*aStreamInfo*/)
{
}