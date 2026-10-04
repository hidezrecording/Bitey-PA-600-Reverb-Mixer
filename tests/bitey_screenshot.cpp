// Bitey GUI screenshot harness — renders the real editor offscreen for
// automated visual verification against the website reference.
// Usage: ./bitey_screenshot [output.png]
// Runs headless under xvfb-run.

#include <juce_gui_extra/juce_gui_extra.h>
#include "../juce/PluginProcessor.h"

class ScreenshotApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "BiteyScreenshot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise(const juce::String& commandLine) override
    {
        juce::String outputFile = "bitey-screenshot.png";
        auto args = juce::StringArray::fromTokens(commandLine, " ", "");
        for (int i = 0; i < args.size(); ++i) {
            if (args[i].endsWith(".png"))
                outputFile = args[i];
        }

        // Create the real processor and editor
        auto proc = std::make_unique<BiteyProcessor>();
        // Prepare with a sensible sample rate (editor doesn't need audio)
        proc->prepareToPlay(44100.0, 512);

        auto* editor = proc->createEditor();
        jassert(editor != nullptr);

        // The editor is 1050x520
        jassert(editor->getWidth() == 1050);
        jassert(editor->getHeight() == 520);

        // Render at 2x for the reference comparison (2100x1040)
        const int scale = 2;
        juce::Image snapshot = editor->createComponentSnapshot(
            editor->getLocalBounds() * scale,
            true,  // clip to bounds
            1.0f   // scale factor handled by bounds
        );

        // Actually createComponentSnapshot with scaled bounds doesn't scale content.
        // Instead, render at 1x and let the comparer upscale, or use an offscreen.
        // For now, do a proper 2x via AffineTransform.
        juce::Image hiRes(juce::Image::ARGB, 1050 * scale, 520 * scale, true);
        {
            juce::Graphics g(hiRes);
            g.addTransform(juce::AffineTransform::scale(scale));
            editor->paintEntireComponent(g, false);
        }

        juce::File outFile(outputFile);
        juce::PNGImageFormat png;
        auto stream = outFile.createOutputStream();
        if (stream) {
            png.writeImageToStream(hiRes, *stream);
            std::printf("Screenshot saved to %s (%dx%d)\n",
                        outputFile.toRawUTF8(), hiRes.getWidth(), hiRes.getHeight());
        } else {
            std::printf("FAILED to write %s\n", outputFile.toRawUTF8());
        }

        delete editor;
        quit();
    }

    void shutdown() override {}
};

START_JUCE_APPLICATION(ScreenshotApp)
