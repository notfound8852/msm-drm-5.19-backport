#include "linux/nvmem-consumer.h"

int nvmem_cell_read_variable_le_u32(struct device *dev,
                                   const char *cell_id,
                                   u32 *val)
{
    struct nvmem_cell *cell;
    const u8 *buf;
    size_t len;
    int i;

    cell = nvmem_cell_get(dev, cell_id);
    if (IS_ERR(cell))
        return PTR_ERR(cell);

    buf = nvmem_cell_read(cell, &len);
    nvmem_cell_put(cell);

    if (IS_ERR(buf))
        return PTR_ERR(buf);

    if (len > sizeof(*val))
        len = sizeof(*val);

    *val = 0;
    for (i = 0; i < len; i++)
        *val |= buf[i] << (i * 8);

    kfree(buf);
    return 0;
}
