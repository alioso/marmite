#pragma once

#include <JuceHeader.h>

#include <mutex>

// Records the final stereo mix to a WAV file on disk, buffered through a
// background TimeSliceThread (juce::AudioFormatWriter::ThreadedWriter) so
// the realtime audio callback never blocks on file I/O for the actual
// write — only a brief, uncontended lock to swap/check the writer
// pointer. That lock is a deliberate, narrow exception to "no locks on
// the audio thread": the alternative (a lock-free pointer swap) would
// leave a genuine use-after-free window between the audio thread reading
// a "still valid" writer pointer and stop() deleting it out from under
// that read. Same justified-exception shape as sampleBuffersMutex_ —
// held briefly, essentially always uncontended, once per audio block
// rather than per sample. Ported from Jerrican's AudioRecorder.h.
class AudioRecorder {
public:
    explicit AudioRecorder(juce::TimeSliceThread& backgroundThread) : backgroundThread_(backgroundThread) {}
    ~AudioRecorder() { stop(); }

    // Called from the message thread (Record button click).
    bool startRecording(const juce::File& file, double sampleRate) {
        stop();
        if (sampleRate <= 0.0) {
            return false;
        }

        file.getParentDirectory().createDirectory();
        file.deleteFile();
        auto fileOutputStream = file.createOutputStream();
        if (fileOutputStream == nullptr) {
            return false;
        }
        std::unique_ptr<juce::OutputStream> fileStream(fileOutputStream.release());

        juce::WavAudioFormat wavFormat;
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate(sampleRate)
                                 .withNumChannels(2)
                                 .withBitsPerSample(16);
        auto writer = wavFormat.createWriterFor(fileStream, options);
        if (writer == nullptr) {
            return false;
        }

        const std::lock_guard<std::mutex> lock(mutex_);
        threadedWriter_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer.release(),
                                                                                     backgroundThread_, 32768);
        return true;
    }

    // Called from the message thread (Record button click, or shutdown).
    void stop() {
        const std::lock_guard<std::mutex> lock(mutex_);
        threadedWriter_.reset();
    }

    bool isRecording() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return threadedWriter_ != nullptr;
    }

    // Called once per audio block, after all processing, with the exact
    // signal being sent to the output device.
    void recordBlock(const float* const* channelData, int numSamples) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (threadedWriter_ != nullptr) {
            threadedWriter_->write(channelData, numSamples);
        }
    }

private:
    juce::TimeSliceThread& backgroundThread_;
    mutable std::mutex mutex_;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
};
