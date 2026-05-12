/* operations_json.c - NDJSON formatting and emitters for opensea-operations
 * Skeleton implementation: format into stack buffer and call custom_Update callback.
 * Implement json escaping and careful snprintf checks here.
 */

#include "code_attributes.h"
#include "common_types.h"
#include "memory_safety.h"
#include "operations.h"
#include "string_utils.h"
/* use common io utilities for safe vsnprintf handling */
#include "io_utils.h"

#include <errno.h>
#include <stdarg.h>

/* Minimal helper: safe append using snprintf; returns new offset or -1 on error */
M_PARAM_RW_SIZE(1, 2)
M_PARAM_RO(4)
FUNC_ATTR_PRINTF(4, 5)
static ssize_t append_snprintf(char* M_NONNULL buf, rsize_t buf_size, ssize_t offset, const char* M_NONNULL fmt, ...)
{
    if (offset < 0 || M_STATIC_CAST(rsize_t, offset) >= buf_size)
    {
        return -1;
    }

    va_list ap;
    va_start(ap, fmt);
    rsize_t remaining = buf_size - M_STATIC_CAST(rsize_t, offset);
    /* use the project's safe wrapper which handles truncation and NUL-termination */
    DISABLE_WARNING_FORMAT_NONLITERAL
    int wrote_count = vsnprintf_err_handle(buf + offset, remaining, fmt, ap);
    RESTORE_WARNING_FORMAT_NONLITERAL
    va_end(ap);

    if (wrote_count < 0)
    {
        return -1;
    }

    /* check for overflow converting to ssize_t */
    rsize_t sum_rsize = M_STATIC_CAST(rsize_t, offset) + M_STATIC_CAST(rsize_t, wrote_count);
    if (sum_rsize > M_STATIC_CAST(rsize_t, SSIZE_MAX))
    {
        return -1;
    }

    return M_STATIC_CAST(ssize_t, sum_rsize);
}

/* TODO: implement proper JSON string escaping (quotes, backslash, control chars).
 * For the skeleton, we provide a placeholder that copies up to max bytes and
 * does minimal escaping for '"' and '\\'.
 */
M_PARAM_RW_SIZE(1, 2)
M_PARAM_RO_SIZE(3, 4)
static int json_escape_copy(char* M_NONNULL out, rsize_t out_size, const char* M_NONNULL in, rsize_t in_len)
{
    if (!out || out_size == 0 || !in)
    {
        return -1;
    }

    rsize_t in_idx  = 0;
    rsize_t out_idx = 0;

    while (in_idx < in_len)
    {
        unsigned char uc = (unsigned char)in[in_idx++];

        /* determine required space for this char's escaped form */
        rsize_t needed = 1;

        if (uc == '"' || uc == '\\')
        {
            needed = 2; /* \" or \\ */
        }
        else if (uc == '\b' || uc == '\f' || uc == '\n' || uc == '\r' || uc == '\t')
        {
            needed = 2; /* \b, \f, \n, \r, \t */
        }
        else if (uc < 0x20)
        {
            needed = 6; /* \u00XX */
        }

        /* ensure room for result + trailing NUL */
        if (out_idx + needed + 1 > out_size)
        {
            return -1;
        }

        switch (uc)
        {
        case '"':
        {
            out[out_idx++] = '\\';
            out[out_idx++] = '"';
            break;
        }

        case '\\':
        {
            out[out_idx++] = '\\';
            out[out_idx++] = '\\';
            break;
        }

        case '\b':
        {
            out[out_idx++] = '\\';
            out[out_idx++] = 'b';
            break;
        }

        case '\f':
        {
            out[out_idx++] = '\\';
            out[out_idx++] = 'f';
            break;
        }

        case '\n':
        {
            out[out_idx++] = '\\';
            out[out_idx++] = 'n';
            break;
        }

        case '\r':
        {
            out[out_idx++] = '\\';
            out[out_idx++] = 'r';
            break;
        }

        case '\t':
        {
            out[out_idx++] = '\\';
            out[out_idx++] = 't';
            break;
        }

        default:
        {
            if (uc < 0x20)
            {
                /* use \u00XX form for other control characters */
                static const char hex[] = "0123456789ABCDEF";
                out[out_idx++]          = '\\';
                out[out_idx++]          = 'u';
                out[out_idx++]          = '0';
                out[out_idx++]          = '0';
                out[out_idx++]          = hex[(uc >> 4) & 0xF];
                out[out_idx++]          = hex[uc & 0xF];
            }
            else
            {
                out[out_idx++] = C_CAST(char, uc);
            }

            break;
        }
        }
    }

    /* ensure NUL termination */
    if (out_idx >= out_size)
    {
        return -1;
    }

    out[out_idx] = '\0';
    return M_STATIC_CAST(int, out_idx);
}

size_t op_format_json_message(const op_json_message* M_NONNULL msg, char* M_NONNULL out_buf, rsize_t out_buf_size)
{
    if (!msg || !out_buf || out_buf_size == 0)
    {
        return 0;
    }

    ssize_t offset = 0;

    /* start object */
    offset = append_snprintf(out_buf, out_buf_size, offset, "{");
    if (offset < 0)
    {
        return 0;
    }

    /* schema_version */
    offset = append_snprintf(out_buf, out_buf_size, offset, "\"schema_version\":\"%s\",", msg->schema_version);
    if (offset < 0)
    {
        return 0;
    }

    /* operation_name */
    DECLARE_ZERO_INIT_ARRAY(char, escaped_opname, OP_JSON_OPERATION_NAME_SIZE * 2);
    int escaped_opname_len = json_escape_copy(escaped_opname, sizeof(escaped_opname), msg->operation_name,
                                              strnlen(msg->operation_name, OP_JSON_OPERATION_NAME_SIZE));
    if (escaped_opname_len < 0)
    {
        return 0;
    }

    offset = append_snprintf(out_buf, out_buf_size, offset, "\"operation_name\":\"%s\",", escaped_opname);
    if (offset < 0)
    {
        return 0;
    }

    /* type */
    const char* type_str = "custom";

    switch (msg->type)
    {
    case OP_JSON_TYPE_PROGRESS:
    {
        type_str = "progress";
        break;
    }

    case OP_JSON_TYPE_ERROR:
    {
        type_str = "error";
        break;
    }

    case OP_JSON_TYPE_STATUS:
    {
        type_str = "status";
        break;
    }

    case OP_JSON_TYPE_STEP:
    {
        type_str = "step";
        break;
    }

    default:
    {
        type_str = "custom";
        break;
    }
    }

    offset = append_snprintf(out_buf, out_buf_size, offset, "\"type\":\"%s\",", type_str);
    if (offset < 0)
    {
        return 0;
    }

    /* optional fields */
    if (msg->has_percent)
    {
        offset = append_snprintf(out_buf, out_buf_size, offset, "\"percent_complete\":%.2f,", msg->percent_complete);
        if (offset < 0)
        {
            return 0;
        }
    }

    if (msg->has_lba)
    {
        offset = append_snprintf(out_buf, out_buf_size, offset, "\"lba\":%" PRIu64 ",", msg->lba);
        if (offset < 0)
        {
            return 0;
        }
    }

    if (msg->has_bytes)
    {
        offset = append_snprintf(out_buf, out_buf_size, offset, "\"bytes_read\":%" PRIu64 ",", msg->bytes_read);
        if (offset < 0)
        {
            return 0;
        }
    }

    if (msg->has_total_bytes)
    {
        offset = append_snprintf(out_buf, out_buf_size, offset, "\"total_bytes\":%" PRIu64 ",", msg->total_bytes);
        if (offset < 0)
        {
            return 0;
        }
    }

    if (msg->has_status)
    {
        offset = append_snprintf(out_buf, out_buf_size, offset, "\"status_code\":%d,", msg->status_code);
        if (offset < 0)
        {
            return 0;
        }
    }

    if (msg->has_estimated_time_seconds)
    {
        offset = append_snprintf(out_buf, out_buf_size, offset, "\"estimated_time_seconds\":%" PRIu64 ",",
                                 msg->estimated_time_seconds);
        if (offset < 0)
        {
            return 0;
        }
    }

    if (msg->has_unit)
    {
        DECLARE_ZERO_INIT_ARRAY(char, escaped_unit, OP_JSON_UNIT_SIZE * 2);
        int escaped_unit_len =
            json_escape_copy(escaped_unit, sizeof(escaped_unit), msg->unit, strnlen(msg->unit, OP_JSON_UNIT_SIZE));
        if (escaped_unit_len < 0)
        {
            return 0;
        }

        offset = append_snprintf(out_buf, out_buf_size, offset, "\"unit\":\"%s\",", escaped_unit);
        if (offset < 0)
        {
            return 0;
        }
    }

    if (msg->has_message)
    {
        DECLARE_ZERO_INIT_ARRAY(char, escaped_message, OP_JSON_MESSAGE_SIZE * 2);
        int escaped_message_len = json_escape_copy(escaped_message, sizeof(escaped_message), msg->message,
                                                   strnlen(msg->message, OP_JSON_MESSAGE_SIZE));
        if (escaped_message_len < 0)
        {
            return 0;
        }

        offset = append_snprintf(out_buf, out_buf_size, offset, "\"message\":\"%s\",", escaped_message);
        if (offset < 0)
        {
            return 0;
        }
    }

    if (msg->has_metadata && msg->metadata)
    {
        rsize_t metadata_len_local = msg->metadata_len;
        if (metadata_len_local == 0)
        {
            metadata_len_local = strnlen(msg->metadata, OP_JSON_MESSAGE_SIZE);
        }

        /* escape into temporary */
        DECLARE_ZERO_INIT_ARRAY(char, escaped_metadata, OP_JSON_MESSAGE_SIZE * 2);
        int escaped_metadata_len =
            json_escape_copy(escaped_metadata, sizeof(escaped_metadata), msg->metadata, metadata_len_local);
        if (escaped_metadata_len < 0)
        {
            return 0;
        }

        offset = append_snprintf(out_buf, out_buf_size, offset, "\"metadata\":\"%s\",", escaped_metadata);
        if (offset < 0)
        {
            return 0;
        }
    }

    /* strip trailing comma if present */
    if (offset > 0 && out_buf[offset - 1] == ',')
    {
        out_buf[offset - 1] = '\0';
        offset -= 1;
    }

    /* close object and newline */
    offset = append_snprintf(out_buf, out_buf_size, offset, "}\n");
    if (offset < 0)
    {
        return 0;
    }

    return M_STATIC_CAST(size_t, offset);
}

void op_emit_json_callback(const op_json_message* M_NONNULL msg,
                           custom_Update M_NONNULL          updateFunc,
                           void* M_NULLABLE                 updateCtx)
{
    DECLARE_ZERO_INIT_ARRAY(char, buf, OP_JSON_LINE_BUF_SIZE);
    size_t len = op_format_json_message(msg, buf, sizeof(buf));
    if (len == 0)
    {
        return; /* formatting error or truncation */
    }

    updateFunc(updateCtx, buf, int_to_sizet(M_STATIC_CAST(int, len)));
}

void op_emit_progress_cb(custom_Update M_NONNULL updateFunc,
                         void* M_NULLABLE        updateCtx,
                         const char* M_NONNULL   operation_name,
                         double                  percent,
                         const char* M_NULLABLE  unit)
{
    op_json_message json_message;
    M_INITIALIZE_STRUCTURE(&json_message, sizeof(op_json_message));
    M_IGNORE_SAFE_ERRNO_CALL(safe_strcpy(json_message.schema_version, OP_JSON_SCHEMA_VER_SIZE, OP_JSON_SCHEMA_VERSION),
                             "Always provides size via enum value used in the destination structure. String version "
                             "will be less than this size and will not overflow the destination.");
    if (0 != safe_strncpy(json_message.operation_name, OP_JSON_OPERATION_NAME_SIZE, operation_name,
                          OP_JSON_OPERATION_NAME_SIZE - 1))
    {
        perror("Error copying operation_name for JSON message. Operation name may be too long.");
        // Note: not returning since this should be ok, even though er received an error as this will lead to an empty
        // name, but we should allow execution to continue and emit whatever we can.
    }
    json_message.type             = OP_JSON_TYPE_PROGRESS;
    json_message.has_percent      = true;
    json_message.percent_complete = percent;

    if (unit)
    {
        json_message.has_unit = true;
        if (0 != safe_strncpy(json_message.unit, OP_JSON_UNIT_SIZE, unit, OP_JSON_UNIT_SIZE - 1))
        {
            perror("Error copying unit for JSON message. Unit may be too long.");
            // Note: not returning since this should be ok and allow returning whatever partial information we can.
            json_message.has_unit = false;
        }
    }

    op_emit_json_callback(&json_message, updateFunc, updateCtx);
}

void op_emit_error_lba_cb(custom_Update M_NONNULL updateFunc,
                          void* M_NULLABLE        updateCtx,
                          const char* M_NONNULL   operation_name,
                          uint64_t                lba,
                          int                     status_code,
                          const char* M_NULLABLE  message)
{
    op_json_message json_message;
    M_INITIALIZE_STRUCTURE(&json_message, sizeof(op_json_message));
    M_IGNORE_SAFE_ERRNO_CALL(safe_strcpy(json_message.schema_version, OP_JSON_SCHEMA_VER_SIZE, OP_JSON_SCHEMA_VERSION),
                             "Always provides size via enum value used in the destination structure. String version "
                             "will be less than this size and will not overflow the destination.");
    if (0 != safe_strncpy(json_message.operation_name, OP_JSON_OPERATION_NAME_SIZE, operation_name,
                          OP_JSON_OPERATION_NAME_SIZE - 1))
    {
        perror("Error copying operation_name for JSON message. Operation name may be too long.");
        // Note: not returning since this should be ok, even though er received an error as this will lead to an empty
        // name, but we should allow execution to continue and emit whatever we can.
    }
    json_message.type        = OP_JSON_TYPE_ERROR;
    json_message.has_lba     = true;
    json_message.lba         = lba;
    json_message.has_status  = true;
    json_message.status_code = status_code;

    if (message)
    {
        json_message.has_message = true;
        if (0 != safe_strncpy(json_message.message, OP_JSON_MESSAGE_SIZE, message, OP_JSON_MESSAGE_SIZE - 1))
        {
            perror("Error copying message for JSON message. Message may be too long.");
            // Note: not returning since this should be ok and allow returning whatever partial information we can.
            json_message.has_message = false;
        }
    }

    op_emit_json_callback(&json_message, updateFunc, updateCtx);
}

void op_emit_step_cb(custom_Update M_NONNULL updateFunc,
                     void* M_NULLABLE        updateCtx,
                     const char* M_NONNULL   operation_name,
                     const char* M_NONNULL   step_message)
{
    op_json_message json_message;
    M_INITIALIZE_STRUCTURE(&json_message, sizeof(op_json_message));
    M_IGNORE_SAFE_ERRNO_CALL(safe_strcpy(json_message.schema_version, OP_JSON_SCHEMA_VER_SIZE, OP_JSON_SCHEMA_VERSION),
                             "Always provides size via enum value used in the destination structure. String version "
                             "will be less than this size and will not overflow the destination.");
    if (0 != safe_strncpy(json_message.operation_name, OP_JSON_OPERATION_NAME_SIZE, operation_name,
                          OP_JSON_OPERATION_NAME_SIZE - 1))
    {
        perror("Error copying operation_name for JSON message. Operation name may be too long.");
        // Note: not returning since this should be ok, even though er received an error as this will lead to an empty
        // name, but we should allow execution to continue and emit whatever we can.
    }
    json_message.type = OP_JSON_TYPE_STEP;

    if (step_message)
    {
        json_message.has_message = true;
        if (0 != safe_strncpy(json_message.message, OP_JSON_MESSAGE_SIZE, step_message, OP_JSON_MESSAGE_SIZE - 1))
        {
            perror("Error copying step_message for JSON message. Step message may be too long.");
            // Note: not returning since this should be ok and allow returning whatever partial information we can.
            json_message.has_message = false;
        }
    }

    op_emit_json_callback(&json_message, updateFunc, updateCtx);
}

void op_emit_custom_cb(custom_Update M_NONNULL          updateFunc,
                       void* M_NULLABLE                 updateCtx,
                       const op_json_message* M_NONNULL msg)
{
    op_emit_json_callback(msg, updateFunc, updateCtx);
}

void op_emit_lba_cb(custom_Update M_NONNULL updateFunc,
                    void* M_NULLABLE        updateCtx,
                    const char* M_NONNULL   operation_name,
                    uint64_t                lba,
                    const char* M_NULLABLE  action)
{
    op_json_message json_message;
    M_INITIALIZE_STRUCTURE(&json_message, sizeof(op_json_message));
    M_IGNORE_SAFE_ERRNO_CALL(safe_strcpy(json_message.schema_version, OP_JSON_SCHEMA_VER_SIZE, OP_JSON_SCHEMA_VERSION),
                             "Always provides size via enum value used in the destination structure. String version "
                             "will be less than this size and will not overflow the destination.");
    if (0 != safe_strncpy(json_message.operation_name, OP_JSON_OPERATION_NAME_SIZE, operation_name,
                          OP_JSON_OPERATION_NAME_SIZE - 1))
    {
        perror("Error copying operation_name for JSON message. Operation name may be too long.");
        // Note: not returning since this should be ok, even though er received an error as this will lead to an empty
        // name, but we should allow execution to continue and emit whatever we can.
    }
    json_message.type    = OP_JSON_TYPE_STATUS;
    json_message.has_lba = true;
    json_message.lba     = lba;
    if (action)
    {
        json_message.has_message = true;
        if (0 != safe_strncpy(json_message.message, OP_JSON_MESSAGE_SIZE, action, OP_JSON_MESSAGE_SIZE - 1))
        {
            perror("Error copying action message for JSON message. Action message may be too long.");
            // Note: not returning since this should be ok and allow returning whatever partial information we can.
            json_message.has_message = false;
        }
    }

    op_emit_json_callback(&json_message, updateFunc, updateCtx);
}
