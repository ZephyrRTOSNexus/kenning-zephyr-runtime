/*
 * Copyright (c) 2023-2024 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <input_data.h>
#include <kenning_inference_lib/core/loaders.h>
#include <kenning_inference_lib/core/model.h>
#include <kenning_inference_lib/core/utils.h>
#include <model_data.h> /* header with model weights generated during build from ./model/<runtime>/ */
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo_app, CONFIG_DEMO_APP_LOG_LEVEL);

/**
 * Magic Wand dataset classes
 */
const char *class_names[] = {"wing", "ring", "slope", "negative"};

/**
 * Helper function for preprocessing input
 */
void preprocess_input(float *data_in, uint8_t *data_out, size_t model_input_size);

/**
 * Helper function for postprocessing output
 */
void postprocess_output(uint8_t *data_in, float *data_out, size_t model_output_size);

/**
 * Helper function for formatting model output
 */
void format_output(uint8_t *buffer, const size_t buffer_size, float *model_output);

int main(void)
{
    status_t status = STATUS_OK;
    uint8_t *model_input = NULL;
    uint8_t *model_output = NULL;
    size_t model_input_size = 0;
    size_t model_output_size = 0;
    float predictions[4];
    uint8_t output_str[512];
    int64_t timer_start = 0;
    int64_t timer_end = 0;

    do
    {
        // initialize model
        status = model_init();
        if (STATUS_OK != status)
        {
            LOG_ERR("Model initialization error 0x%x (%s)", status, get_status_str(status));
            break;
        }

        // load model structure
        status = model_load_struct((uint8_t *)&model_struct, sizeof(MlModel));
        BREAK_ON_ERROR_LOG(status, "Model struct load error 0x%x (%s)", status, get_status_str(status));

        // load model weights
        status = model_load_weights(model_data, model_data_len);
        BREAK_ON_ERROR_LOG(status, "Model weights load error 0x%x (%s)", status, get_status_str(status));

        // allocate buffer for input
        model_get_input_size(&model_input_size);
        model_input = k_aligned_alloc(32, model_input_size);

        // allocate buffer for output
        model_get_output_size(&model_output_size);
        model_output = k_aligned_alloc(32, model_output_size);

        // inference loop
        timer_start = k_uptime_get();
        for (size_t batch_index = 0; batch_index < sizeof(data) / sizeof(data[0]); ++batch_index)
        {
            preprocess_input((float *)data[batch_index], model_input, model_input_size);

            status = model_load_input(model_input, model_input_size);
            BREAK_ON_ERROR_LOG(status, "Model input load error 0x%x (%s)", status, get_status_str(status));

            status = model_run();
            BREAK_ON_ERROR_LOG(status, "Model run error 0x%x (%s)", status, get_status_str(status));

            status = model_get_output(model_output_size, model_output, NULL);
            BREAK_ON_ERROR_LOG(status, "Model get output error 0x%x (%s)", status, get_status_str(status));

            postprocess_output(model_output, predictions, model_output_size);

            format_output(output_str, sizeof(output_str), predictions);
            LOG_INF("model output: %s", output_str);
        }
        timer_end = k_uptime_get();
    } while (0);

    if (IS_VALID_POINTER(model_output))
    {
        free(model_output);
    }
    if (IS_VALID_POINTER(model_input))
    {
        free(model_input);
    }
    if (STATUS_OK == status)
    {
        LOG_INF("inference done in %lld ms (%lld ms per batch)", timer_end - timer_start,
                (timer_end - timer_start) * sizeof(data[0]) / sizeof(data));
    }
    return 0;
}

void format_output(uint8_t *buffer, const size_t buffer_size, float *model_output)
{
    uint8_t *buffer_end = buffer + buffer_size;

    buffer += snprintf(buffer, buffer_end - buffer, "[");
    for (int i = 0; i < model_struct.output_length[0]; ++i)
    {
        if (i > 0)
        {
            buffer += snprintf(buffer, buffer_end - buffer, ", ");
        }
        buffer += snprintf(buffer, buffer_end - buffer, "%s: ", class_names[i]);
        buffer += snprintf(buffer, buffer_end - buffer, "%f", (double)model_output[i]);
    }
    buffer += snprintf(buffer, buffer_end - buffer, "]");
}

#ifndef CONFIG_KENNING_DEMO_USE_QUANTIZED_MODEL

void preprocess_input(float *restrict data_in, uint8_t *restrict data_out, size_t model_input_size)
{
    memcpy(data_out, data_in, model_input_size);
}

void postprocess_output(uint8_t *restrict data_in, float *restrict data_out, size_t model_output_size)
{
    memcpy(data_out, data_in, model_output_size);
}

#else // CONFIG_KENNING_DEMO_USE_QUANTIZED_MODEL

void preprocess_input(float *restrict data_in, uint8_t *restrict data_out, size_t model_input_size)
{
    for (int i = 0; i < model_input_size; ++i)
    {
        data_out[i] = (int8_t)(data_in[i] / (float)QUANTIZATION_INPUT_SCALE + (float)QUANTIZATION_INPUT_ZERO_POINT);
    }
}

void postprocess_output(uint8_t *restrict data_in, float *restrict data_out, size_t model_output_size)
{
    for (int i = 0; i < model_output_size; ++i)
    {
        data_out[i] =
            ((float)(int8_t)data_in[i] - (float)QUANTIZATION_OUTPUT_ZERO_POINT) * (float)QUANTIZATION_OUTPUT_SCALE;
    }
}

#endif // CONFIG_KENNING_DEMO_USE_QUANTIZED_MODEL
