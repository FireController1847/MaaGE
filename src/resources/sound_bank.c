#include "maage/sound_bank.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#include "audio_decode/stb_vorbis.c"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAAGE_SOUND_TABLE_COUNT ((size_t)MAAGE_SOUND_MAX_CLIP_ID + 1U)
#define MAAGE_SOUND_DECODE_CHUNK 4096U

typedef struct maage_sound_slot {
    char *path;
    float *cached_samples;
    size_t cached_bytes;
    size_t frame_count;
    unsigned int channels, sample_rate;
    int has_failure;
    maage_status failure;
    struct maage_sound_slot *previous, *next;
} maage_sound_slot;

struct maage_sound_bank {
    char *data_root;
    maage_sound_slot **slots;
    size_t budget, retained_bytes;
    maage_sound_slot *most_recent, *least_recent;
};

static maage_status maage_sound_result(unsigned char code,
    unsigned char operation, maage_status_trace *trace)
{
    return MAAGE_STATUS_PROPAGATE(trace,
        maage_status_make(code, MAAGE_CONTEXT_RESOURCE, operation, 0),
        MAAGE_CONTEXT_RESOURCE, operation);
}

static void maage_sound_cache_unlink(maage_sound_bank *bank, maage_sound_slot *slot)
{
    if (!slot->cached_samples) return;
    if (slot->previous) slot->previous->next = slot->next;
    else bank->most_recent = slot->next;
    if (slot->next) slot->next->previous = slot->previous;
    else bank->least_recent = slot->previous;
    slot->previous = NULL;
    slot->next = NULL;
    bank->retained_bytes -= slot->cached_bytes;
    free(slot->cached_samples);
    slot->cached_samples = NULL;
    slot->cached_bytes = 0;
}

static void maage_sound_cache_front(maage_sound_bank *bank, maage_sound_slot *slot)
{
    /* A linked slot is always backed by bank-owned, immutable PCM. */
    if (slot != bank->most_recent) {
        if (slot->previous) slot->previous->next = slot->next;
        if (slot->next) slot->next->previous = slot->previous;
        if (bank->least_recent == slot) bank->least_recent = slot->previous;
        slot->previous = NULL;
        slot->next = bank->most_recent;
        if (bank->most_recent) bank->most_recent->previous = slot;
        else bank->least_recent = slot;
        bank->most_recent = slot;
    }
}

static void maage_sound_cache_store(maage_sound_bank *bank,
    maage_sound_slot *slot, const maage_sound_pcm *pcm)
{
    float *copy;
    size_t bytes;
    if (!bank->budget || pcm->frame_count > (size_t)-1 /
        ((size_t)pcm->channels * sizeof(float))) return;
    bytes = pcm->frame_count * (size_t)pcm->channels * sizeof(float);
    if (!bytes || bytes > bank->budget) return;
    /* Allocation failure never changes a successful decoded result. */
    copy = (float *)malloc(bytes);
    if (!copy) return;
    memcpy(copy, pcm->samples, bytes);
    while (bank->least_recent && bank->retained_bytes > bank->budget - bytes)
        maage_sound_cache_unlink(bank, bank->least_recent);
    slot->cached_samples = copy;
    slot->cached_bytes = bytes;
    slot->frame_count = pcm->frame_count;
    slot->channels = pcm->channels;
    slot->sample_rate = pcm->sample_rate;
    bank->retained_bytes += bytes;
    maage_sound_cache_front(bank, slot);
}

/* Decode only at application scheduling/asset boundaries, never in an audio
 * callback. stb_vorbis may allocate scratch; it does not own caller PCM.
 * Input and output sizes are bounded separately, including malformed streams
 * whose advertised frame count is untrustworthy. */
static maage_status maage_sound_decode_file(const char *path,
    maage_sound_pcm *output, maage_status_trace *trace)
{
    FILE *file;
    stb_vorbis *vorbis;
    stb_vorbis_info info;
    unsigned char signature[4], ogg_header[27], lacing[255], identification[7];
    unsigned int segment_count;
    unsigned int declared_frames;
    unsigned int rate, channels;
    int decoder_error, received, final_error, input_error, close_error;
    float samples_chunk[MAAGE_SOUND_DECODE_CHUNK * 2U];
    float *samples = NULL, *resized;
    size_t frames = 0, capacity = 0, needed, grown, max_frames;
    size_t bytes;
    double encoded_size;
    char fingerprint[128];
    maage_status result;
    unsigned char code;

    result = maage_storage_stat(path, &encoded_size, fingerprint);
    if (maage_status_failed(result)) {
        if (maage_status_code(result) == MAAGE_STATUS_NOT_FOUND)
            return maage_sound_result(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_DECODE, trace);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE,
            MAAGE_OPERATION_DECODE);
    }
    if (encoded_size < 4.0)
        return maage_sound_result(MAAGE_STATUS_MALFORMED_INPUT,
            MAAGE_OPERATION_DECODE, trace);
    if (encoded_size > (double)MAAGE_SOUND_MAX_OGG_BYTES)
        return maage_sound_result(MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_OPERATION_DECODE, trace);

    errno = 0;
    file = maage_storage_open(path, "rb");
    if (!file) {
        int saved_errno = errno ? errno : EIO;
        result = maage_status_from_errno(saved_errno, MAAGE_CONTEXT_STORAGE,
            MAAGE_OPERATION_OPEN);
        return maage_status_trace_record(trace, result, MAAGE_CONTEXT_STORAGE,
            MAAGE_OPERATION_OPEN, __FILE__, (unsigned long)__LINE__, saved_errno);
    }
    if (fread(signature, 1, sizeof(signature), file) != sizeof(signature)) {
        code = ferror(file) ? MAAGE_STATUS_IO_ERROR : MAAGE_STATUS_MALFORMED_INPUT;
        fclose(file);
        return maage_sound_result(code, MAAGE_OPERATION_DECODE, trace);
    }
    if (memcmp(signature, "OggS", sizeof(signature))) {
        fclose(file);
        return maage_sound_result(MAAGE_STATUS_NOT_IMPLEMENTED,
            MAAGE_OPERATION_DECODE, trace);
    }
    /* The 1.7.10 sound bank requires Vorbis, not merely an Ogg container.
     * Distinguish unsupported Ogg codecs from corrupt/truncated Vorbis. */
    rewind(file);
    if (fread(ogg_header, 1, sizeof(ogg_header), file) != sizeof(ogg_header)) {
        code = ferror(file) ? MAAGE_STATUS_IO_ERROR : MAAGE_STATUS_MALFORMED_INPUT;
        fclose(file);
        return maage_sound_result(code, MAAGE_OPERATION_DECODE, trace);
    }
    segment_count = (unsigned int)ogg_header[26];
    if (ogg_header[4] || !(ogg_header[5] & 2U) || !segment_count ||
        fread(lacing, 1, segment_count, file) != segment_count) {
        code = ferror(file) ? MAAGE_STATUS_IO_ERROR : MAAGE_STATUS_MALFORMED_INPUT;
        fclose(file);
        return maage_sound_result(code, MAAGE_OPERATION_DECODE, trace);
    }
    if (lacing[0] < sizeof(identification) ||
        fread(identification, 1, sizeof(identification), file) != sizeof(identification)) {
        code = ferror(file) ? MAAGE_STATUS_IO_ERROR : MAAGE_STATUS_MALFORMED_INPUT;
        fclose(file);
        return maage_sound_result(code, MAAGE_OPERATION_DECODE, trace);
    }
    if (identification[0] != 1 || memcmp(identification + 1, "vorbis", 6)) {
        fclose(file);
        return maage_sound_result(MAAGE_STATUS_NOT_IMPLEMENTED,
            MAAGE_OPERATION_DECODE, trace);
    }
    rewind(file);
    decoder_error = VORBIS__no_error;
    /* With close_handle_on_close=0 we retain and close the MaaGE Storage FILE. */
    vorbis = stb_vorbis_open_file(file, 0, &decoder_error, NULL);
    if (!vorbis) {
        code = decoder_error == VORBIS_outofmem ? MAAGE_STATUS_ALLOCATION_FAILED :
            (decoder_error == VORBIS_feature_not_supported ||
             decoder_error == VORBIS_too_many_channels ?
             MAAGE_STATUS_NOT_IMPLEMENTED : MAAGE_STATUS_MALFORMED_INPUT);
        input_error = ferror(file);
        fclose(file);
        return maage_sound_result(input_error ? MAAGE_STATUS_IO_ERROR : code,
            MAAGE_OPERATION_DECODE, trace);
    }

    info = stb_vorbis_get_info(vorbis);
    channels = (unsigned int)info.channels;
    rate = (unsigned int)info.sample_rate;
    result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DECODE, 0);
    if (channels < 1U || channels > 2U || rate < 8000U || rate > 192000U) {
        result = maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED,
            MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
    } else {
        max_frames = (size_t)MAAGE_SOUND_MAX_PCM_BYTES /
            sizeof(float) / (size_t)channels;
        declared_frames = stb_vorbis_stream_length_in_samples(vorbis);
        decoder_error = stb_vorbis_get_error(vorbis);
        if (decoder_error != VORBIS__no_error)
            result = maage_status_make(decoder_error == VORBIS_outofmem ?
                MAAGE_STATUS_ALLOCATION_FAILED : MAAGE_STATUS_MALFORMED_INPUT,
                MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
        else if ((double)declared_frames > (double)max_frames)
            result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
                MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);

        while (!maage_status_failed(result)) {
            received = stb_vorbis_get_samples_float_interleaved(vorbis,
                (int)channels, samples_chunk, (int)(MAAGE_SOUND_DECODE_CHUNK * channels));
            decoder_error = stb_vorbis_get_error(vorbis);
            if (decoder_error != VORBIS__no_error || received < 0 ||
                (unsigned int)received > MAAGE_SOUND_DECODE_CHUNK) {
                code = decoder_error == VORBIS_outofmem ? MAAGE_STATUS_ALLOCATION_FAILED :
                    (decoder_error == VORBIS_feature_not_supported ||
                     decoder_error == VORBIS_too_many_channels ?
                     MAAGE_STATUS_NOT_IMPLEMENTED : MAAGE_STATUS_MALFORMED_INPUT);
                result = maage_status_make(code, MAAGE_CONTEXT_RESOURCE,
                    MAAGE_OPERATION_DECODE, 0);
                break;
            }
            if (!received) break;
            if ((size_t)received > max_frames - frames) {
                result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
                    MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
                break;
            }
            needed = frames + (size_t)received;
            if (needed > capacity) {
                grown = capacity ? capacity : MAAGE_SOUND_DECODE_CHUNK;
                while (grown < needed) {
                    if (grown >= max_frames / 2U) { grown = max_frames; break; }
                    grown *= 2U;
                }
                if (grown < needed || grown > max_frames ||
                    grown > (size_t)-1 / ((size_t)channels * sizeof(float))) {
                    result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
                        MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
                    break;
                }
                bytes = grown * (size_t)channels * sizeof(float);
                resized = (float *)realloc(samples, bytes);
                if (!resized) {
                    result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
                        MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
                    break;
                }
                samples = resized;
                capacity = grown;
            }
            memcpy(samples + frames * (size_t)channels, samples_chunk,
                (size_t)received * (size_t)channels * sizeof(float));
            frames = needed;
        }
        if (!maage_status_failed(result) &&
            (!frames || (declared_frames && frames != (size_t)declared_frames)))
            result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT,
                MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
    }

    final_error = stb_vorbis_get_error(vorbis);
    input_error = ferror(file);
    stb_vorbis_close(vorbis);
    close_error = fclose(file);
    if (!maage_status_failed(result) && (final_error || input_error || close_error))
        result = maage_status_make(input_error || close_error ?
            MAAGE_STATUS_IO_ERROR : (final_error == VORBIS_outofmem ?
            MAAGE_STATUS_ALLOCATION_FAILED : MAAGE_STATUS_MALFORMED_INPUT),
            MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
    if (maage_status_failed(result)) {
        free(samples);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE,
            MAAGE_OPERATION_DECODE);
    }
    output->samples = samples;
    output->frame_count = frames;
    output->channels = channels;
    output->sample_rate = rate;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DECODE, 0);
}

maage_status maage_sound_bank_create(const char *data_root, size_t memory_budget,
    maage_sound_bank **output, maage_status_trace *trace)
{
    maage_sound_bank *bank;
    char root[MAAGE_PATH_CAPACITY];
    char probe[MAAGE_PATH_CAPACITY];
    size_t count;
    maage_status result;
    if (!output || *output)
        return maage_sound_result(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_CREATE, trace);
    if (!data_root) {
        result = maage_storage_data_path(root, sizeof(root));
        if (maage_status_failed(result))
            return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE,
                MAAGE_OPERATION_CREATE);
        data_root = root;
    }
    /* Make the same root/security checks as the authoritative path resolver. */
    result = maage_minecraft_asset_path(data_root,
        "minecraft/sounds/__maage_probe__.ogg", probe, sizeof(probe), trace);
    if (maage_status_failed(result))
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE,
            MAAGE_OPERATION_CREATE);
    if (MAAGE_SOUND_TABLE_COUNT > (size_t)-1 / sizeof(maage_sound_slot *))
        return maage_sound_result(MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_OPERATION_CREATE, trace);
    count = strlen(data_root) + 1;
    bank = (maage_sound_bank *)calloc(1, sizeof(*bank));
    if (!bank)
        return maage_sound_result(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_OPERATION_CREATE, trace);
    bank->data_root = (char *)malloc(count);
    bank->slots = (maage_sound_slot **)calloc(MAAGE_SOUND_TABLE_COUNT,
        sizeof(*bank->slots));
    if (!bank->data_root || !bank->slots) {
        free(bank->data_root);
        free(bank->slots);
        free(bank);
        return maage_sound_result(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_OPERATION_CREATE, trace);
    }
    memcpy(bank->data_root, data_root, count);
    bank->budget = memory_budget;
    *output = bank;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}

maage_status maage_sound_bank_register(maage_sound_bank *bank,
    unsigned int numeric_clip_id, const char *logical_asset_path,
    maage_status_trace *trace)
{
    maage_sound_slot *slot;
    char path[MAAGE_PATH_CAPACITY];
    const char *extension;
    size_t length;
    maage_status result;
    if (!bank || !logical_asset_path || numeric_clip_id > MAAGE_SOUND_MAX_CLIP_ID)
        return maage_sound_result(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_REGISTER, trace);
    if (bank->slots[numeric_clip_id])
        return maage_sound_result(MAAGE_STATUS_INVALID_STATE,
            MAAGE_OPERATION_REGISTER, trace);
    extension = strrchr(logical_asset_path, '.');
    if (!extension || strcmp(extension, ".ogg"))
        return maage_sound_result(MAAGE_STATUS_NOT_IMPLEMENTED,
            MAAGE_OPERATION_REGISTER, trace);
    result = maage_minecraft_asset_path(bank->data_root, logical_asset_path,
        path, sizeof(path), trace);
    if (maage_status_failed(result))
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE,
            MAAGE_OPERATION_REGISTER);
    slot = (maage_sound_slot *)calloc(1, sizeof(*slot));
    if (!slot)
        return maage_sound_result(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_OPERATION_REGISTER, trace);
    length = strlen(path) + 1U;
    slot->path = (char *)malloc(length);
    if (!slot->path) {
        free(slot);
        return maage_sound_result(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_OPERATION_REGISTER, trace);
    }
    memcpy(slot->path, path, length);
    bank->slots[numeric_clip_id] = slot;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}

maage_status maage_sound_bank_decode(maage_sound_bank *bank,
    unsigned int numeric_clip_id, maage_sound_pcm *pcm,
    maage_status_trace *trace)
{
    maage_sound_slot *slot;
    maage_sound_pcm decoded;
    maage_status result;
    float *copy;
    if (!bank || !pcm || pcm->samples || pcm->frame_count ||
        pcm->channels || pcm->sample_rate || numeric_clip_id > MAAGE_SOUND_MAX_CLIP_ID)
        return maage_sound_result(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_DECODE, trace);
    slot = bank->slots[numeric_clip_id];
    if (!slot)
        return maage_sound_result(MAAGE_STATUS_NOT_FOUND,
            MAAGE_OPERATION_DECODE, trace);
    if (slot->has_failure)
        return MAAGE_STATUS_PROPAGATE(trace, slot->failure, MAAGE_CONTEXT_RESOURCE,
            MAAGE_OPERATION_DECODE);
    if (slot->cached_samples) {
        copy = (float *)malloc(slot->cached_bytes);
        if (!copy)
            return maage_sound_result(MAAGE_STATUS_ALLOCATION_FAILED,
                MAAGE_OPERATION_DECODE, trace);
        memcpy(copy, slot->cached_samples, slot->cached_bytes);
        pcm->samples = copy;
        pcm->frame_count = slot->frame_count;
        pcm->channels = slot->channels;
        pcm->sample_rate = slot->sample_rate;
        maage_sound_cache_front(bank, slot);
        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DECODE, 0);
    }
    memset(&decoded, 0, sizeof(decoded));
    result = maage_sound_decode_file(slot->path, &decoded, trace);
    if (maage_status_failed(result)) {
        /* The resource generation is immutable: only memory pressure is
         * transient. Retain missing, corrupt, inaccessible and I/O failures
         * so no playback path repeatedly probes the same failed resource. */
        if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED) {
            slot->has_failure = 1;
            slot->failure = result;
        }
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE,
            MAAGE_OPERATION_DECODE);
    }
    maage_sound_cache_store(bank, slot, &decoded);
    *pcm = decoded;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DECODE, 0);
}

maage_status maage_sound_pcm_destroy(maage_sound_pcm *pcm)
{
    int had_samples;
    if (!pcm) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    had_samples = pcm->samples != NULL;
    free(pcm->samples);
    memset(pcm, 0, sizeof(*pcm));
    return maage_status_make(had_samples ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP,
        0, MAAGE_OPERATION_DESTROY, 0);
}

maage_status maage_sound_bank_destroy(maage_sound_bank *bank)
{
    size_t i;
    maage_sound_slot *slot;
    if (!bank) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    for (i = 0; i < MAAGE_SOUND_TABLE_COUNT; ++i) {
        slot = bank->slots[i];
        if (!slot) continue;
        maage_sound_cache_unlink(bank, slot);
        free(slot->path);
        free(slot);
    }
    free(bank->slots);
    free(bank->data_root);
    free(bank);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
