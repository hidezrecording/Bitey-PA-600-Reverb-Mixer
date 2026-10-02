// BiteyTestHost — standalone listening rig for the Bitey PA-600.
// Not part of the shipped plugin: a dev/test host that loads an audio file,
// runs it through the real BiteyAudioProcessor + editor, and plays it out.
//
// Build: added as a juce_add_gui_app target in juce/CMakeLists.txt (macOS CI).
// Usage: launch, click "Load Audio File…", then Play.

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../PluginProcessor.h"

// ---------------------------------------------------------------------------
// HostComponent — transport bar on top, live Bitey editor below.
// Audio path: file -> AudioTransportSource -> BiteyAudioProcessor -> output.
// ---------------------------------------------------------------------------
class HostComponent : public juce::Component,
                      public juce::AudioIODeviceCallback,
                      public juce::Button::Listener
{
public:
    HostComponent()
    {
        formatManager.registerBasicFormats();

        loadButton.setButtonText("Load Audio File…");
        loadButton.addListener(this);
        addAndMakeVisible(loadButton);

        playButton.setButtonText("Play");
        playButton.addListener(this);
        playButton.setEnabled(false);
        addAndMakeVisible(playButton);

        stopButton.setButtonText("Stop");
        stopButton.addListener(this);
        stopButton.setEnabled(false);
        addAndMakeVisible(stopButton);

        fileLabel.setText("No file loaded — click “Load Audio File…”",
                          juce::dontSendNotification);
        fileLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(fileLabel);

        // The real plugin processor + editor, exactly as a DAW would host it.
        biteyProc = std::make_unique<BiteyAudioProcessor>();
        biteyEditor.reset(biteyProc->createEditor());
        jassert(biteyEditor != nullptr);
        addAndMakeVisible(biteyEditor.get());

        deviceManager.initialise(0, 2, nullptr, true, {}, nullptr);
        deviceManager.addAudioCallback(this);

        const int editorW = biteyEditor->getWidth();
        const int editorH = biteyEditor->getHeight();
        setSize(editorW, transportH + editorH);
    }

    ~HostComponent() override
    {
        deviceManager.removeAudioCallback(this);
        transport.setSource(nullptr);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop(transportH);
        loadButton.setBounds(bar.removeFromLeft(150).reduced(6));
        playButton.setBounds(bar.removeFromLeft(80).reduced(6));
        stopButton.setBounds(bar.removeFromLeft(80).reduced(6));
        fileLabel.setBounds(bar.reduced(6));
        if (biteyEditor)
            biteyEditor->setBounds(area);
    }

    // --- Button::Listener ---
    void buttonClicked(juce::Button* b) override
    {
        if (b == &loadButton)      chooseFile();
        else if (b == &playButton)  { transport.start(); }
        else if (b == &stopButton)  { transport.stop(); transport.setPosition(0.0); }
    }

    // --- AudioIODeviceCallback ---
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        const double sr = device->getCurrentSampleRate();
        const int bs = device->getCurrentBufferSizeSamples();
        biteyProc->prepareToPlay(sr, bs);
        transport.prepareToPlay(bs, sr);
    }

    void audioDeviceStopped() override
    {
        transport.releaseResources();
        biteyProc->releaseResources();
    }

    void audioDeviceIOCallback(const float** /*in*/, int /*numIn*/,
                               float** out, int numOut, int numSamples) override
    {
        juce::AudioBuffer<float> buffer(out, numOut, numSamples);
        buffer.clear();
        juce::AudioSourceChannelInfo info(buffer);
        transport.getNextAudioBlock(info);   // file audio into buffer
        juce::MidiBuffer midi;
        biteyProc->processBlock(buffer, midi); // through the real plugin DSP
    }

private:
    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser>(
            "Load audio file to run through Bitey",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& c)
            {
                juce::File f = c.getResult();
                if (f == juce::File{}) return;
                if (auto* reader = formatManager.createReaderFor(f))
                {
                    readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
                    transport.setSource(readerSource.get(), 0, nullptr, reader->sampleRate);
                    transport.setPosition(0.0);
                    fileLabel.setText(f.getFileName(), juce::dontSendNotification);
                    playButton.setEnabled(true);
                    stopButton.setEnabled(true);
                }
            });
    }

    static constexpr int transportH = 44;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<BiteyAudioProcessor> biteyProc;
    std::unique_ptr<juce::AudioProcessorEditor> biteyEditor;

    juce::TextButton loadButton, playButton, stopButton;
    juce::Label fileLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostComponent)
};

// ---------------------------------------------------------------------------
class BiteyTestHostApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Bitey Test Host"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override                               { mainWindow = nullptr; }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name, juce::Colours::black,
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new HostComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(BiteyTestHostApp)
