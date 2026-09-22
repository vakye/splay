
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct wav_audio
{
    void*               SampleData;
    unsigned int        SampleRate;
    unsigned int        ChannelCount;
    unsigned int        BitsPerSample;
    unsigned int        SampleCount;
} wav_audio;

static void PrintUsage(void);
static int OpenWAV(char* FilePath, wav_audio* Result);

int main(int ArgCount, char* Args[])
{
    if (ArgCount == 1)
    {
        PrintUsage();
    }
    else
    {
        // NOTE(vak): Parse configuration

        char* AudioFilePath = 0;
        int LoopEnabled = 0;

        for (int ArgIndex = 1; ArgIndex < ArgCount; ArgIndex++)
        {
            if ((strcmp(Args[ArgIndex], "-l")       == 0) ||
                (strcmp(Args[ArgIndex], "--loop")   == 0))
            {
                LoopEnabled = 1;
            }
            else if ((strcmp(Args[ArgIndex], "-h")      == 0) ||
                     (strcmp(Args[ArgIndex], "--help")  == 0))
            {
                PrintUsage();
                return (0);
            }
            else
            {
                if (AudioFilePath)
                {
                    fprintf(stderr, "error: splay only accepts one .wav audio file.\n");
                    return (1);
                }

                AudioFilePath = Args[ArgIndex];
            }
        }

        if (!AudioFilePath)
        {
            fprintf(stderr, "error: No .wav audio file supplied.\n");
            return (1);
        }

        wav_audio Audio = {0};

        if (!OpenWAV(AudioFilePath, &Audio))
            return (1);

        #define PCM_DEVICE "default"        

        int Error = 0;

        snd_pcm_t* PCM = 0;
        Error = snd_pcm_open(&PCM, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
        if (Error < 0)
        {
            fprintf(stderr, "error: Can't open '%s'. %s\n", PCM_DEVICE, snd_strerror(Error));
            return (1);
        }

        snd_pcm_hw_params_t* Params = 0;
        snd_pcm_hw_params_alloca(&Params);
        snd_pcm_hw_params_any(PCM, Params);

        Error = snd_pcm_hw_params_set_access(PCM, Params, SND_PCM_ACCESS_RW_INTERLEAVED);
        if (Error < 0)
        {
            fprintf(stderr, "error: Can't set interleaved mode. %s\n", snd_strerror(Error));
            return (1);
        }

        int FormatPCM = 0;

        if (Audio.BitsPerSample == 16)
        {
            FormatPCM = SND_PCM_FORMAT_S16_LE;
        }
        else if (Audio.BitsPerSample == 24)
        {
            FormatPCM = SND_PCM_FORMAT_S24_3LE;
        }
        else
        {
            fprintf(stderr, "error: Unknown audio format with BitsPerSample = %u\n", Audio.BitsPerSample);
            return (1);
        }

        Error = snd_pcm_hw_params_set_format(PCM, Params, FormatPCM);
        if (Error < 0)
        {
            fprintf(stderr, "error: Can't set format. %s\n", snd_strerror(Error));
            return (1);
        }

        Error = snd_pcm_hw_params_set_channels(PCM, Params, Audio.ChannelCount);
        if (Error < 0)
        {
            fprintf(stderr, "error: Can't set channel count to %u. %s\n", Audio.ChannelCount, snd_strerror(Error));
            return (1);
        }

        Error = snd_pcm_hw_params_set_rate_near(PCM, Params, &Audio.SampleRate, 0);
        if (Error < 0)
        {
            fprintf(stderr, "error: Can't set sample rate to %u. %s\n", Audio.SampleRate, snd_strerror(Error));
            return (1);
        }

        snd_pcm_uframes_t Frames = 8192;
        Error = snd_pcm_hw_params_set_period_size_near(PCM, Params, &Frames, 0);
        if (Error < 0)
        {
            fprintf(stderr, "error: Can't set period size to %lu. %s\n", Frames, snd_strerror(Error));
            return (1);
        }

        Error = snd_pcm_hw_params(PCM, Params);
        if (Error < 0)
        {
            fprintf(stderr, "error: Can't set hardware parameters. %s\n", snd_strerror(Error));
            return (1);
        }

        snd_pcm_hw_params_get_period_size(Params, &Frames, 0);

        unsigned int BytesPerSample = (Audio.BitsPerSample * Audio.ChannelCount) / 8;
        unsigned int PeriodBytes = (Frames * Audio.BitsPerSample * Audio.ChannelCount) / 8;
        unsigned int PeriodTime = 0;

        snd_pcm_hw_params_get_period_time(Params, &PeriodTime, 0);

        unsigned int Microseconds;
        char* At;
        struct timespec BeginTime = {0};
        struct timespec Now = {0};
        unsigned int SentMicroseconds;

DoItAgain:

        clock_gettime(CLOCK_MONOTONIC, &BeginTime);
        Microseconds =  (unsigned int)(1000000 * (float)Audio.SampleCount / (float)Audio.SampleRate);
        At = (char*)Audio.SampleData;
        SentMicroseconds = 0;

        for (int Loops = Microseconds / PeriodTime; Loops > 0; Loops--)
        {
            printf("Sending (remaining %i)...\n", Loops);
            Error = snd_pcm_writei(PCM, At, Frames);
            if (Error < 0)
            {
                fprintf(stderr, "error: Can't write to PCM device. %s\n", snd_strerror(Error));
                return (1);
            }

            At += PeriodBytes;
            SentMicroseconds += PeriodTime;

            clock_gettime(CLOCK_MONOTONIC, &Now);

            intptr_t MicrosecondsElapsed =
                (Now.tv_sec - BeginTime.tv_sec) * 1000000 +
                (Now.tv_nsec - BeginTime.tv_nsec) / 1000;

            intptr_t BufferedMicroseconds = SentMicroseconds - MicrosecondsElapsed;

            while (BufferedMicroseconds > (intptr_t)PeriodTime/2)
            {
                usleep((BufferedMicroseconds - PeriodTime/2)/2);
                clock_gettime(CLOCK_MONOTONIC, &Now);

                MicrosecondsElapsed =
                    (Now.tv_sec - BeginTime.tv_sec) * 1000000 +
                    (Now.tv_nsec - BeginTime.tv_nsec) / 1000;

                BufferedMicroseconds = SentMicroseconds - MicrosecondsElapsed;
            }
        }

        {
            intptr_t LeftoverSamples = Audio.SampleCount % Frames;

            printf("Sending (remaining 0)...\n");
            Error = snd_pcm_writei(PCM, At, LeftoverSamples);
            if (Error < 0)
            {
                fprintf(stderr, "error: Can't write to PCM device. %s\n", snd_strerror(Error));
                return (1);
            }

            SentMicroseconds += (intptr_t)(((double)LeftoverSamples / (double)Frames) * PeriodTime);
        }

        clock_gettime(CLOCK_MONOTONIC, &Now);

        intptr_t MicrosecondsElapsed =
            (Now.tv_sec - BeginTime.tv_sec) * 1000000 +
            (Now.tv_nsec - BeginTime.tv_nsec) / 1000;

        intptr_t BufferedMicroseconds = SentMicroseconds - MicrosecondsElapsed;

        while (BufferedMicroseconds > 0)
        {
            clock_gettime(CLOCK_MONOTONIC, &Now);

            MicrosecondsElapsed =
                (Now.tv_sec - BeginTime.tv_sec) * 1000000 +
                (Now.tv_nsec - BeginTime.tv_nsec) / 1000;

            BufferedMicroseconds = SentMicroseconds - MicrosecondsElapsed;
        }

        if (LoopEnabled)
            goto DoItAgain;
    }

    return (0);
}

static void PrintUsage(void)
{
    printf("Usage: splay [Flags] <.wav Audio File>\n");
    printf("Flags:\n");
    printf("    -l, --loop: Enable loop playback.\n");
    printf("    -h, --help: Print usage.\n");
}

typedef struct
{
    unsigned int    FileTypeID;
    unsigned int    FileSize;
    unsigned int    FileFormatID;

    unsigned int    FormatBlockID;
    unsigned int    BlockSize;
    unsigned short  AudioFormat;
    unsigned short  ChannelCount;
    unsigned int    SampleRate;
    unsigned int    BytesPerSecond;
    unsigned short  BytesPerBlock;
    unsigned short  BitsPerSample;
} wav_header;

static int OpenWAV(char* FilePath, wav_audio* Result)
{
    FILE* File = fopen(FilePath, "rb");
    if (!File)
    {
        fprintf(stderr, "error: Failed to open '%s'\n", FilePath);
        return (0);
    }

    fseek(File, 0, SEEK_END);
    unsigned int FileSize = ftell(File);
    fseek(File, 0, SEEK_SET);

    void* FileData = calloc(1, FileSize);
    fread(FileData, FileSize, 1, File);

    fclose(File);

    wav_header* Header = (wav_header*)FileData;

    if (Header->FileTypeID != 0x46464952)
    {
        fprintf(stderr, "error: File type ID is not 'RIFF' in .wav file.\n");
        return (0);
    }

    if (Header->FileFormatID != 0x45564157)
    {
        fprintf(stderr, "error: File format ID is not 'WAVE' in .wav file.\n");
        return (0);
    }

    if (Header->FormatBlockID != 0x20746d66)
    {
        fprintf(stderr, "error: Format block ID is not 'fmt ' .wav file.\n");
        return (0);
    }

    unsigned int DataBlockOffset = 0;

    for (unsigned int Index = 0; Index < FileSize; Index++)
    {
        unsigned int Value = *(unsigned int*)((unsigned char*)FileData + Index);

        if (Value == 0x61746164)
        {
            DataBlockOffset = Index;
            break;
        }
    }

    if (!DataBlockOffset)
    {
        fprintf(stderr, "error: Data block not found within .wav file.\n");
        return (0);
    }

    unsigned int DataSize = *(unsigned int*)((char*)FileData + DataBlockOffset + 4);

    Result->SampleData      = ((char*)FileData + DataBlockOffset + 8);
    Result->SampleRate      = Header->SampleRate;
    Result->ChannelCount    = Header->ChannelCount;
    Result->BitsPerSample   = Header->BitsPerSample;
    Result->SampleCount     = DataSize / ((Header->BitsPerSample * Header->ChannelCount) / 8);

    return (1);
}

