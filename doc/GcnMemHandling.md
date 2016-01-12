## GCN Memory instructions features and functionality

### Buffer resource format

Bits   | Name       | Description
-------|------------|------------------------------
0-47   | BASE       | Base address
48-61  | STRIDE     | Stride in bytes. Size of records
62     | Cache swizzle |  Buffer access. Optionally, swizzle texture cache TC L1 cache banks
63     | Swizzle enable | If set, enable swizzle addressing mode
64-95  | NUMRECORDS | Number of records (size of the buffer)
96-98  | DST_SEL_X  | Select destination component for X
99-101 | DST_SEL_Y  | Select destination component for Y
102-104 | DST_SEL_Z  | Select destination component for Z
105-107 | DST_SEL_W  | Select destination component for W
108-110 | NUMFORMAT  | Number format
111-114 | DATAFORMAT | Data format
115-116 | ELEMSIZE   | Element size (used only by swizzle mode)
117-118 | INDEXSTRIDE | Index stride (used only by swizzle mode)
119    | TID_ENABLE | Add thread id to index
121    | Hash enable | Enable address hashing 
122    | HEAP       | Buffer is heap
126-127 | TYPE      | Resource type. 0 - for buffer


### MUBUF/MTBUF format conversion

The instruction or the buffer resource can supply data format in which stored
(or will be stored) data. The data format determines format of the pixel (number of
components, size of components), the number format determines format of the component.

Below is table with available data formats:

Code | Name          | Description
-----|---------------|-------------------------
 0   | --            | Invalid
 1   | 8             | Single 8-bit component
 2   | 16            | Single 16-bit component
 3   | 8_8           | Two 8-bit components
 4   | 32            | Single 32-bit component
 5   | 16_16         | Two 16-bit component
 6   | 10_11_11      | Two 11-bit and one 10-bit components from lowest bit
 7   | 11_11_10      | One 10-bit and two 11-bit components from lowest bit
 8   | 10_10_10_2    | One 2-bit and three 10-bit components from lowest bit
 9   | 2_10_10_10    | Three 10-bit and one 2-bit components from lowest bit
 10  | 8_8_8_8       | Four 8-bit components
 11  | 32_32         | Two 32-bit components
 12  | 16_16_16_16   | Four 16-bit components
 13  | 32_32_32      | Three 32-bit components
 14  | 32_32_32_32   | Four 32-bit components
 15  | --            | Reserved

The buffer data format name can be preceded by 'BUF_DATA_FORMAT_' as 'BUF_DATA_FORMAT_8_8'.
A data format name is case-insensitive.

The data format 10_11_11 and 11_11_10 seemingly doesn't work correctly on the GCN 1.0 (???)

Below is table with available number formats. The 'BufR' column indicates whether
a number format is applicable to read operation, the 'BufW' column indicates whether
a number format is applicable to write operation. The 'Reg type' indicates type of
vector register (input for writing, output for reading).

Code | Name      | BufR | BufW | Reg type | Description
-----|-----------|------|------|----------|--------------------------------
 0   | UNORM     |   ✓  |   ✓  | FLOAT    | Unsigned normalized value (0:1)
 1   | SNORM     |   ✓  |   ✓  | FLOAT    | Signed normalized value (-1:1) (data: MIN+1:MAX)
 2   | USCALED   |   ✓  |      | FLOAT    | Unsigned scaled value
 3   | SSCALED   |   ✓  |      | FLOAT    | Signed scaled value
 4   | UINT      |   ✓  |   ✓  | UINT32   | Unsigned integer value
 5   | SINT      |   ✓  |   ✓  | INT32    | Signed integer value
 6   | SNORM_OGL |   ✓  |      | FLOAT    | Signed normalized value (-1:1) (data: MIN:MAX)
 7   | FLOAT     |   ✓  |   ✓  | FLOAT    | Single floating point value

The buffer number format name can be preceded by 'BUF_NUM_FORMAT_' as
'BUF_NUM_FORMAT_UNORM'. A number format name is case-insensitive.
The FLOAT number float is applicable to 32, 32_32, 32_32_32 or 32_32_32_32 data format.
The conversion from integer to floating point value while writing to buffer
is doing with rounding to nearest even.

The fields DST_SEL_X, DST_SEL_Y, DST_SEL_Z and DST_SEL_W choose how the source component
will be stored into the destination component. DST_SEL_X choose for the first component,
DST_SEL_Y for second, DST_SEL_Z for third, DST_SEL_W for fourth.
Following values are permitted:

Code | Name | Description
-----|------|----------------------
 0   |  0   | Zero value
 1   |  1   | One value
 4   |  R   | First source component
 5   |  G   | Second source component
 6   |  B   | Third source component
 7   |  A   | Fourth source component

The rules for data conversion for particular instruction types:

 Instruction            | Data format | Num format  | DST_SEL_*
------------------------|-------------|-------------|-------------------
TBUFFER_LOAD_FORMAT_*   | instruction | instruction | identity
TBUFFER_STORE_FORMAT_*  | instruction | instruction | identity
BUFFER_LOAD_<type>      | derived     | derived     | identity
BUFFER_STORE_<type>     | derived     | derived     | identity
BUFFER_LOAD_FORMAT_*    | resource    | resource    | resource
BUFFER_STORE_FORMAT_*   | resource    | resource    | resource
BUFFER_ATOMIC_*         | derived     | derived     | identity

* instruction - thing determined from instruction's fields instead of the buffer resource
* derived - data format derived from opcode and ignores resource definition
* identity - choose this same source component for destination component
* resource - thing determined from buffer resource

### Buffer addressing

The address depends on couple factors which are from instruction and the buffer resource.
The buffer is organized in simple structure that contains records. Buffer address starts
from the base offset given in buffer resource. The index indicates number of the record,
and the offset indicates position in record. Expression that describes address:

```
BUFOFFSET = (UINT32)(AINDEX*STRIDE) + AOFFSET
```

Optionally, buffer can be addressed in the swizzle mode that is very effective manner to
addressing the scratch buffers that holds spilled registers. Expression that describes
swizzle mode addressing:

```
AINDEX_MSB = AINDEX / INDEXSTRIDE
AINDEX_LSB = AINDEX % INDEXSTRIDE
AOFFSET_MSB = AOFFSET / ELEMSIZE
AOFFSET_LSB = AOFFSET % ELEMSIZE
BUFOFFSET = AOFFSET_LSB + ELEMSIZE*AINDEX_LSB + INDEXSTRIDE * (AINDEX_MSB*STRIDE + \
        AOFFSET_MSB * ELEMSIZE)
```

The expression to calculate element size (ELEMSIZE) from ELEMSIZE field of a
buffer resource is: 2<<ELEMSIZE. The expression to calculate index stride (INDEXSTRIDE)
from INDEXSTRIDE field of buffer resource is: 8<<INDEXSTRIDE.

The 64-bit addressing can be enabled by set ADDR64 flag in instruction. In this case,
two VADDR registers contains an address. Expression to calculate address in this case:

```
ADDRESS = BASE + VGPR_ADDRESS + OFFSET + SGPR_OFFSET // 64-bit addressing
```

No range checking for 64-bit mode addressing.

The base address are calculated as sum of BASE and SGPR offset. The instruction format
supply OFFEN and IDXEN that includes index from VPGR registers. These flags are permitted
only if 64-bit addressing is not enabled. Table describes combination of these flags:

 IDXEN |OFFEN | Indexreg | Offset reg 
-------|------|----------|------------------------
   0   |  0   | N/A      | N/A
   1   |  0   | VGPR[V]  | N/A
   0   |  1   | N/A      | VGPR[V]
   1   |  1   | VGPR[V]  | VGPR[V+1]

Expressions that describes offsets and indices:

```
UINT32 AOFFSET = OFFSET + (OFFEN ? VGPR_OFFSET : 0)
UINT32 AINDEX = (IDXEN ? VGPR_INDEX : 0) + (TID_ENABLE ? LANEID : 0)
```

The hardware checks range for buffer resources with STRIDE=0 in following way:
if BUFOFFSET >= NUMRECORDS-SGPR_OFFSET then an address is out of range.
For STRIDE!=0 if AINDEX >= NUMRECORDS or OFFSET >= STRIDE when IDXEN or
TID_ENABLE is set, then an address is out of range. Reads are zero and writes are ignored
for an addresses out of range.

For 32-bit and wider operations, an address are aligned to 4 bytes.
For 16-bit operations, an address are aligned to 2 bytes.

The coalescing works for STRIDE==0 on offset (hardware looks at offset), otherwise it works
if stride<=1 or swizzle mode enabled and all offsets are equal and ELEMSIZE have same value
as size of element that can be operated by instruction. Then hardware coalesce across any
set of contiguous indices for raw buffers. For swizzled buffers, it
cannot coalesce across INDEXSTRIDE boundaries.

Reading data to LDS directly is working for BUFFER_LOAD_UBYTE, BUFFER_LOAD_SBYTE,
BUFFER_LOAD_SSHORT, BUFFER_LOAD_USHORT, BUFFER_LOAD_DWORD and BUFFER_LOAD_FORMAT_X
instructions. If element size is smaller than 4-byte, then stored 4-byte value will be
zero-extended. Mixing TFE and LDS flags together is illegal.
LDS address are calculated from that expression:

```
LDS_ADDR = LDS_BASE + (M0&0xffff) + LANEID*4
```

* LDS_BASE - base address of LDS for wave.
* M0 - M0 register value

### Image resource format

The image resource can be a 128-bit (for 2D images, 1D images, 2DMSAA images) or
256 (for 3D images, 2D array of images, 2D cubes and other types).

Bits   | Name       | Description
-------|------------|------------------------------
0-39   | BASE       | Base address divided by 256
40-51  | MINLOD     | Min LOD in format 4.8
52-57  | DATAFORMAT | Data format
58-61  | NUMBERFORMAT | Number format
64-77  | WIDTH      | Image width minus one
78-91  | HEIGHT     | Image height minus one
92-94  | PERFMOD    | Scales sampler's perf_z, perf_mip, aniso_bias, lod_bias_sec.
95     | INTERLACED | Interlaced image (if set)
96-98  | DST_SEL_X  | Select destination component for X
99-101 | DST_SEL_Y  | Select destination component for Y
102-104 | DST_SEL_Z  | Select destination component for Z
105-107 | DST_SEL_W  | Select destination component for W
108-111 | BASELEVEL | Base level of MIPMAP
112-115 | LASTLAVEL | Last level of MIPMAP
116-120 | TILINGINDEX | Tiling index. Choose Tiling register
121     | POW2PAD    | Align images to power of 2
124-127 | TYPE      | Image type
128-140 | DEPTH     | Image depth minus one
141-154 | PITCH     | Pitch minus one. Pitch is number of texel in single row.
160-172 | BASEARRAY | First index of slice in array
173-185 | LASTARRAY | Last index of slice in array
192-203 | MINLODWARN | feedback trigger for LOD

The 1D images requires only width parameter.
The 2D images requires only width and height parameters. Pitch is optional.
The array of 1D images requires width, depth (for number of slices),
base and last array (BASEARRAY and LASTARRAY) indices for slices.
The array of 2D images requires width, height, depth (for number of slices),
base and last array (BASEARRAY and LASTARRAY) indices for slices.
The 3D array images requires width, height and depth.
The 2D cubes requires width, height and base and last array indices of slices.
The mipmaps are defined by setting base and last level (BASE_LEVEL and LAST_LEVEL).

The image types list.

 Value | Name      | Description
-------|-----------|--------------------
 0     | BUFFER    | Buffer (???)
 8     | IMAGE_1D  | 1D image
 9     | IMAGE_2D  | 2D image
 10    | IMAGE_3D  | 3D image
 11    | IMAGE_CUBE | Six 2D images for cube's sides
 12    | IMAGE_1D_ARRAY | Array of 1D images
 13    | IMAGE_2D_ARRAY | Array of 2D images
 14    | IMAGE_2D_MSAA | MSAA 2D image
 15    | IMAGE_2D_MSAA_ARRAY | Array of MSAA 2D image

Data formats list. The 'ImgR' column indicates whether
a number format is applicable to read operation, the 'ImgW' column indicates whether
a number format is applicable to write operation. The 'Reg type' indicates type of
vector register (input for writing, output for reading).

Code | Name          | ImgR | ImgW | Description
-----|---------------|------|------|-------------------------
 0   | --            |      |      | Invalid
 1   | 8             |   ✓  |   ✓  | Single 8-bit component
 2   | 16            |   ✓  |   ✓  | Single 16-bit component
 3   | 8_8           |   ✓  |   ✓  | Two 8-bit components
 4   | 32            |   ✓  |   ✓  | Single 32-bit component
 5   | 16_16         |   ✓  |   ✓  | Two 16-bit component
 6   | 10_11_11      |   ✓  |   ✓  | Two 11-bit and one 10-bit components from lowest bit
 7   | 11_11_10      |   ✓  |   ✓  | One 10-bit and two 11-bit components from lowest bit
 8   | 10_10_10_2    |   ✓  |   ✓  | One 2-bit and three 10-bit components from lowest bit
 9   | 2_10_10_10    |   ✓  |   ✓  | Three 10-bit and one 2-bit components from lowest bit
 10  | 8_8_8_8       |   ✓  |   ✓  | Four 8-bit components
 11  | 32_32         |   ✓  |   ✓  | Two 32-bit components
 12  | 16_16_16_16   |   ✓  |   ✓  | Four 16-bit components
 13  | 32_32_32      |   ✓  |   ✓  | Three 32-bit components
 14  | 32_32_32_32   |   ✓  |   ✓  | Four 32-bit components
 15  | --            |   ✓  |   ✓  | Reserved
 16  | 5_6_5         |   ✓  |   ✓  | 5-bit, 6-bit, 5-bit components
 17  | 1_5_5_5       |   ✓  |   ✓  | Three 5-bit and one 1-bit components from lowest bit
 18  | 5_5_5_1       |   ✓  |   ✓  | One 1-bit and three 5-bit components from lowest bit
 19  | 4_4_4_4       |   ✓  |   ✓  | Four 4-bit components
 20  | 8_24          |   ✓  |      | 24-bit and 8-bit components from lowest bit
 21  | 24_8          |   ✓  |      | 8-bit and 24-bit components from lowest bit
 22  | X24_8_32      |   ✓  |      | ????
 32  | GB_GR         |   ✓  |      | Four 8-bit components in order (0,1,2,0)
 33  | BG_RG         |   ✓  |      | Four 8-bit components in order (1,0,3,1)
 34  | 5_9_9_9       |   ✓  |      | Three 9-bit and one 5-bit components from lowest bit
 35  | BC1           |   ✓  |      | ????
 36  | BC2           |   ✓  |      | ????
 37  | BC3           |   ✓  |      | ????
 38  | BC4           |   ✓  |      | ????
 39  | BC5           |   ✓  |      | ????
 40  | BC6           |   ✓  |      | ????
 41  | BC7           |   ✓  |      | ????
 47  | FMASK_8_1     |   ✓  |   ✓  | 8-bit FMASK, 1 fragment per sample
 48  | FMASK_8_2     |   ✓  |   ✓  | 8-bit FMASK, 2 fragments per sample
 49  | FMASK_8_4     |   ✓  |   ✓  | 8-bit FMASK, 4 fragments per sample
 50  | FMASK_16_1    |   ✓  |   ✓  | 16-bit FMASK, 1 fragment per sample
 51  | FMASK_16_2    |   ✓  |   ✓  | 16-bit FMASK, 2 fragments per sample
 52  | FMASK_32_2    |   ✓  |   ✓  | 32-bit FMASK, 2 fragments per sample
 53  | FMASK_32_4    |   ✓  |   ✓  | 32-bit FMASK, 4 fragments per sample
 54  | FMASK_32_8    |   ✓  |   ✓  | 32-bit FMASK, 8 fragments per sample
 55  | FMASK_64_4    |   ✓  |   ✓  | 64-bit FMASK, 4 fragments per sample
 56  | FMASK_64_8    |   ✓  |   ✓  | 64-bit FMASK, 8 fragments per sample
 57  | 4_4           |   ✓  |      | Two 4-bit components
 58  | 6_5_5         |   ✓  |      | Two 5-bit and one 6-bit components from lowest bit
 59  | 1             |   ✓  |      | 1-bit component (size: 1-bit)
 60  | 1_REVERSED    |   ✓  |      | Reversed 1-bit component (size: 1-bit)
 61  | 32_AS_8       |   ✓  |      | ???
 62  | 32_AS_8_8     |   ✓  |      | ???
 63  | 32_AS_32_32_32_32 |   ✓  |      | ???

Number formats list:

Code | Name      | ImgR | ImgW | Reg type | Description
-----|-----------|------|------|----------|--------------------------------
 0   | UNORM     |   ✓  |   ✓  | FLOAT    | Unsigned normalized value (0:1)
 1   | SNORM     |   ✓  |   ✓  | FLOAT    | Signed normalized value (-1:1) (data: MIN+1:MAX)
 2   | USCALED   |   ✓  |      | FLOAT    | Unsigned scaled value
 3   | SSCALED   |   ✓  |      | FLOAT    | Signed scaled value
 4   | UINT      |   ✓  |   ✓  | UINT32   | Unsigned integer value
 5   | SINT      |   ✓  |   ✓  | INT32    | Signed integer value
 6   | SNORM_OGL |   ✓  |      | FLOAT    | Signed normalized value (-1:1) (data: MIN:MAX)
 7   | FLOAT     |   ✓  |   ✓  | FLOAT    | Single floating point value
 8   | reserved  |      |      | --       | --
 9   | SRGB      |   ✓  |      | FLOAT    | Like UNORM (???)
 10  | UBNORM    |   ✓  |      | FLOAT    | Signed normalized value (-1:1) (data: 1:UMAX)
 11  | UBNORM_OGL |   ✓  |      | FLOAT    | Signed normalized value (-1:1) (data: 0:UMAX)
 12  | UBINT     |   ✓  |      | FLOAT    | Like UBNORM (???)
 13  | UBSCALED   |   ✓  |      | FLOAT    | Signed scaled value (data: 0:UMAX)

### Sampler resource format



### Image addressing

The main addressing rules for the images are defined by the tiling registers.
The TILINGINDEX choose what register control addressing of image. Index 8 (by default)
choose the linear access. In the most cases images are splitted into the tiles which
organizes image's data in efficient manner for GPU memory subsystem. Unfortunatelly,
the fields of a tiling registers and their meanigful are not known (for me).