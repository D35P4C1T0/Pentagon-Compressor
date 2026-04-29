#include <iostream>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

namespace
{
int writeEditorScreenshot(const juce::File& outputFile)
{
    juce::ScopedJuceInitialiser_GUI juceGui;

    PentagonAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    editor->setVisible(true);
    editor->resized();

    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);

    if (! image.isValid())
    {
        std::cerr << "Failed to create editor snapshot\n";
        return 1;
    }

    outputFile.getParentDirectory().createDirectory();

    juce::FileOutputStream stream(outputFile);

    if (! stream.openedOk())
    {
        std::cerr << "Failed to open " << outputFile.getFullPathName() << " for writing\n";
        return 1;
    }

    juce::PNGImageFormat png;

    if (! png.writeImageToStream(image, stream))
    {
        std::cerr << "Failed to encode PNG\n";
        return 1;
    }

    std::cout << "Wrote " << outputFile.getFullPathName() << "\n";
    return 0;
}
} // namespace

int main(int argc, char* argv[])
{
    const auto output = argc > 1
        ? juce::File(argv[1])
        : juce::File::getCurrentWorkingDirectory().getChildFile("docs/pentagon-ui.png");

    return writeEditorScreenshot(output);
}
