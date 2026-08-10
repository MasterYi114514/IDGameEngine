#include "Loader/IAssetLoader.hpp"
#include "Loader/AudioLoader.hpp"

#include "Log.hpp"

#include <cstring>
#include <filesystem>

namespace
{
    using namespace ID;

    struct WAVHeader
    {
        char     riff[4];         // "RIFF"
        uint32_t chunk_size;
        char     wave[4];         // "WAVE"
        char     fmt[4];          // "fmt "
        uint32_t fmt_size;
        uint16_t audio_format;    // 1 = PCM
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
        char     data[4];         // "data"
        uint32_t data_size;
    };

    static_assert(sizeof(WAVHeader) == 44, "WAV header must be 44 bytes");

    AudioData load_audio_data(const std::string& path)
    {
        AudioData data;

        if (!std::filesystem::exists(path))
        {
            IDASSET_ERROR("AudioLoader::load：文件不存在: {}", path);
            return data;
        }

        FILE* file = fopen(path.c_str(), "rb");
        if (!file)
        {
            IDASSET_ERROR("AudioLoader::load：无法打开文件: {}", path);
            return data;
        }

        WAVHeader header;
        if (fread(&header, sizeof(WAVHeader), 1, file) != 1)
        {
            IDASSET_ERROR("AudioLoader::load：读取 WAV 头失败: {}", path);
            fclose(file);
            return data;
        }

        // 验证 RIFF/WAVE 标记
        if (std::memcmp(header.riff, "RIFF", 4) != 0 || std::memcmp(header.wave, "WAVE", 4) != 0)
        {
            IDASSET_ERROR("AudioLoader::load：非有效 WAV 文件: {}", path);
            fclose(file);
            return data;
        }

        // 仅支持 PCM 格式
        if (header.audio_format != 1)
        {
            IDASSET_ERROR("AudioLoader::load：WAV 非 PCM 格式: {}", path);
            fclose(file);
            return data;
        }

        // 读取 PCM 数据
        data.pcm_data.resize(header.data_size);
        if (fread(data.pcm_data.data(), 1, header.data_size, file) != header.data_size)
        {
            IDASSET_ERROR("AudioLoader::load：读取 PCM 数据失败: {}", path);
            fclose(file);
            data.pcm_data.clear();
            return data;
        }
        fclose(file);

        data.sample_rate    = header.sample_rate;
        data.channels       = static_cast<uint8_t>(header.num_channels);
        data.bits_per_sample = header.bits_per_sample;

        // 计算时长
        data.duration = static_cast<float>(header.data_size)
                      / static_cast<float>(header.sample_rate * header.num_channels * (header.bits_per_sample / 8));

        return data;
    }
} // 匿名命名空间

namespace ID
{
    Asset<AudioData> AudioLoader::load(const std::string& path)
    {
        AudioData data = load_audio_data(path);
        if (data.is_valid())
        {
            return Asset<AudioData>{AssetState::Loaded, std::move(data), path};
        }
        else
        {
            return Asset<AudioData>{AssetState::Failed, AudioData{}, path};
        }
    }

    void AudioLoader::reload(Asset<AudioData>& asset)
    {
        if (asset.path.empty())
        {
            IDASSET_ERROR("AudioLoader::reload：资源路径为空");
            asset.set_failed();
            return;
        }

        asset.reset();
        AudioData data = load_audio_data(asset.path);
        if (data.is_valid())
        {
            asset.data = std::move(data);
            asset.set_loaded();
        }
        else
        {
            asset.set_failed();
        }
    }
} // namespace ID
