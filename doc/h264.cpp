/**
 * 最简单的视音频数据处理示例
 * Simplest MediaData Test
 *
 * 雷霄骅 Lei Xiaohua
 * leixiaohua1020@126.com
 * 中国传媒大学/数字电视技术
 * Communication University of China / Digital TV Technology
 * http://blog.csdn.net/leixiaohua1020
 *
 * 本项目包含如下几种视音频测试示例：
 *  (1)像素数据处理程序。包含RGB和YUV像素格式处理的函数。
 *  (2)音频采样数据处理程序。包含PCM音频采样格式处理的函数。
 *  (3)H.264码流分析程序。可以分离并解析NALU。
 *  (4)AAC码流分析程序。可以分离并解析ADTS帧。
 *  (5)FLV封装格式分析程序。可以将FLV中的MP3音频码流分离出来。
 *  (6)UDP-RTP协议分析程序。可以将分析UDP/RTP/MPEG-TS数据包。
 *
 * This project contains following samples to handling multimedia data:
 *  (1) Video pixel data handling program. It contains several examples to handle RGB and YUV data.
 *  (2) Audio sample data handling program. It contains several examples to handle PCM data.
 *  (3) H.264 stream analysis program. It can parse H.264 bitstream and analysis NALU of stream.
 *  (4) AAC stream analysis program. It can parse AAC bitstream and analysis ADTS frame of stream.
 *  (5) FLV format analysis program. It can analysis FLV file and extract MP3 audio stream.
 *  (6) UDP-RTP protocol analysis program. It can analysis UDP/RTP/MPEG-TS Packet.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ============================================================================
 * H.264 / AVC 基础概念
 * ============================================================================
 *
 * H.264 码流由一系列 NALU（NAL Unit，网络抽象层单元）组成。
 * 每个 NALU 之间用"起始码"（Start Code）分隔。
 *
 * Annex B 格式的起始码有两种：
 *   - 0x00 00 01 （3字节起始码）
 *   - 0x00 00 00 01 （4字节起始码，通常用于 SPS/PPS/IDR 等关键数据）
 *
 * 每个 NALU 的第一个字节是 NAL Header（NAL 头部），结构如下：
 *   +---------------+---------------+
 *   | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |  (bit 位)
 *   +---------------+---------------+
 *   |F  |  NRI  |       Type        |
 *   +-------------------------------+
 *   - forbidden_bit (F) : 1 bit，恒为 0，为 1 表示有传输错误
 *   - nal_reference_idc (NRI): 2 bit，优先级，值越大越重要
 * 		优先级：该 NALU 是否被参考帧引用，0=可丢弃，非0=被参考（非0的帧不能随意丢弃）
 *   - nal_unit_type (Type): 5 bit，标识 NALU 的类型（共 32 种），例如：
 *       1: 非 IDR 图像的编码条带（P帧/B帧的编码数据）
 *       5: IDR 图像的编码条带（I帧，关键帧，解码器遇到它会清空参考帧缓冲）
 *       7: 序列参数集（SPS，Sequence Parameter Set），含分辨率、编码档次等信息
 *       8: 图像参数集（PPS，Picture Parameter Set），含熵编码模式、量化参数等	
 */
1
// ==================== NALU 类型枚举 ====================
// nal_unit_type 的取值，标识这个 NALU 携带的是什么数据
typedef enum {
	NALU_TYPE_SLICE    = 1,  // 非 IDR 图像的编码条带（P帧/B帧的编码数据）
	NALU_TYPE_DPA      = 2,  // 数据分区 A（Data Partition A）
	NALU_TYPE_DPB      = 3,  // 数据分区 B
	NALU_TYPE_DPC      = 4,  // 数据分区 C
	NALU_TYPE_IDR      = 5,  // IDR 图像的编码条带（I帧，关键帧，解码器遇到它会清空参考帧缓冲）
	NALU_TYPE_SEI      = 6,  // 补充增强信息（SEI，Supplemental Enhancement Information）
	NALU_TYPE_SPS      = 7,  // 序列参数集（SPS，Sequence Parameter Set），含分辨率、编码档次等信息
	NALU_TYPE_PPS      = 8,  // 图像参数集（PPS，Picture Parameter Set），含熵编码模式、量化参数等
	NALU_TYPE_AUD      = 9,  // 访问单元分隔符（AUD，Access Unit Delimiter），标志一个完整帧的开始
	NALU_TYPE_EOSEQ    = 10, // 序列结束（End of Sequence）
	NALU_TYPE_EOSTREAM = 11, // 码流结束（End of Stream）
	NALU_TYPE_FILL     = 12, // 填充数据（Filler Data），用于填充码率达到指定码率
} NaluType;

// ==================== NALU 优先级枚举 ====================
// nal_reference_idc 的取值，标识该 NALU 的重要程度
typedef enum {
	NALU_PRIORITY_DISPOSABLE = 0, // 可丢弃（B帧数据等，解码时丢掉了不影响其他帧）
	NALU_PRIRITY_LOW         = 1, // 低优先级
	NALU_PRIORITY_HIGH       = 2, // 高优先级
	NALU_PRIORITY_HIGHEST    = 3  // 最高优先级（SPS/PPS/IDR 等关键数据）
} NaluPriority;


/*
 * ==================== NALU 结构体 ====================
 * 描述一个完整的 NAL Unit
 */
typedef struct
{
	int startcodeprefix_len;      // 起始码长度：参数集和图像首条带用 4 字节，其余用 3 字节
	unsigned len;                 // NALU 数据长度（不含起始码，起始码不属于 NALU）
	unsigned max_size;            // NALU 缓冲区大小（预分配的 buf 容量）
	int forbidden_bit;            // 禁止位（F），正常应为 0，1 表示有传输错误
	int nal_reference_idc;        // 参考优先级（NRI），值越大表示该 NALU 越不可丢弃
	int nal_unit_type;            // NALU 类型，对应 NaluType 枚举
	char *buf;                    // 指向 EBSP 数据（起始码之后的第一个字节开始）
	                              // EBSP = 原始字节序列载荷（Encapsulated Byte Sequence Payload）
} NALU_t;

FILE *h264bitstream = NULL;                //!< 打开的 H.264 码流文件指针

int info2=0, info3=0;                     // 查找起始码时的标志：info2=1 表示找到3字节起始码，info3=1 表示找到4字节起始码

/*
 * ==================== 查找 3 字节起始码 ====================
 * 检测 Buf[0..2] 是否为 0x00 00 01
 * 这是 H.264 Annex B 格式中最常见的起始码
 */
static int FindStartCode2 (unsigned char *Buf){
	if(Buf[0]!=0 || Buf[1]!=0 || Buf[2] !=1) return 0; // 不是 0x000001
	else return 1;
}

/*
 * ==================== 查找 4 字节起始码 ====================
 * 检测 Buf[0..3] 是否为 0x00 00 00 01
 * 4 字节起始码通常出现在 SPS、PPS、IDR 帧等关键数据前
 * （H.264 编码器默认会在每个完整帧前插入 4 字节起始码）
 */
static int FindStartCode3 (unsigned char *Buf){
	if(Buf[0]!=0 || Buf[1]!=0 || Buf[2] !=0 || Buf[3] !=1) return 0;// 不是 0x00000001
	else return 1;
}


/*
 * ==================== 从码流中提取一个完整 NALU ====================
 *
 * 这是整个程序的核心函数。工作流程：
 *
 *   1. 从文件当前位置读取 3~4 字节，判断起始码类型
 *       ├── 如果是 0x00 00 01 → 3字节起始码，NALU 数据从第4字节开始
 *       └── 如果是 0x00 00 00 01 → 4字节起始码，NALU 数据从第5字节开始
 *
 *   2. 逐个字节向后扫描，直到找到下一个起始码
 *       └── 两个起始码之间的数据就是一个完整的 NALU
 *
 *   3. 解析 NALU 的第一个字节（NAL Header）：
 *       +--+--+--+--+--+--+--+--+
 *       |F |  NRI  |   Type     |
 *       +--+--+--+--+--+--+--+--+
 *       bit7: forbidden_bit    (0x80 掩码)
 *       bit6-5: nal_reference_idc (0x60 掩码)
 *       bit4-0: nal_unit_type  (0x1F 掩码)
 *
 *   4. 文件指针回退，以便下次调用时从下一个起始码开始读
 *
 * 图示（Annex B 码流结构）：
 *   ... | 00 00 00 01 |  NALU 数据  | 00 00 01 |  NALU 数据  | 00 00 00 01 | ...
 *        ↑ 起始码      ↑            ↑ 起始码   ↑            ↑ 起始码
 *
 * @param nalu 输出参数，存放解析后的 NALU 信息
 * @return     本次读取的总字节数（起始码长度 + NALU 数据长度），出错返回 0 或 -1
 */
int GetAnnexbNALU (NALU_t *nalu){
	int pos = 0;                           // Buf 的当前写入位置
	int StartCodeFound, rewind;            // StartCodeFound: 是否找到下一个起始码; rewind: 文件指针回退量
	unsigned char *Buf;

	// 分配临时缓冲区，大小为 NALU 的最大可能长度
	if ((Buf = (unsigned char*)calloc (nalu->max_size , sizeof(char))) == NULL)
		printf ("GetAnnexbNALU: Could not allocate Buf memory\n");

	nalu->startcodeprefix_len=3;           // 先假设是 3 字节起始码

	// ---- 第 1 步：尝试读取起始码 ----
	// 先读 3 个字节，看是不是 0x00 00 01
	if (3 != fread (Buf, 1, 3, h264bitstream)){
		free(Buf);
		return 0;
	}
	info2 = FindStartCode2 (Buf);
	if(info2 != 1) {
		// 不是 3 字节起始码，再读 1 个字节，检查是不是 4 字节起始码 0x00 00 00 01
		if(1 != fread(Buf+3, 1, 1, h264bitstream)){
			free(Buf);
			return 0;
		}
		info3 = FindStartCode3 (Buf);
		if (info3 != 1){
			// 既不是 3 字节也不是 4 字节起始码 → 码流格式错误
			free(Buf);
			return -1;
		}
		else {
			// 找到 4 字节起始码 0x00 00 00 01
			pos = 4;
			nalu->startcodeprefix_len = 4;
		}
	}
	else{
		// 找到 3 字节起始码 0x00 00 01
		nalu->startcodeprefix_len = 3;
		pos = 3;                           // NALU 数据从 Buf[3] 开始
	}

	// ---- 第 2 步：逐字节扫描，找下一个起始码（作为当前 NALU 的结束标志）----
	StartCodeFound = 0;
	info2 = 0;
	info3 = 0;

	while (!StartCodeFound){
		// 如果读到文件末尾，说明这是最后一个 NALU
		if (feof (h264bitstream)){
			// 最后一个 NALU 的长度 = (当前读到的位置 - 1) - 起始码长度
			nalu->len = (pos-1)-nalu->startcodeprefix_len;
			// 将 NALU 数据（跳过起始码）拷贝到 nalu->buf
			memcpy (nalu->buf, &Buf[nalu->startcodeprefix_len], nalu->len);
			// ---- 第 3 步：解析 NAL Header（第一个字节）----
			nalu->forbidden_bit = nalu->buf[0] & 0x80;     // 取最高位 (bit7)
			nalu->nal_reference_idc = nalu->buf[0] & 0x60; // 取 bit6-5
			nalu->nal_unit_type = (nalu->buf[0]) & 0x1f;   // 取 bit4-0
			free(Buf);
			return pos-1;
		}
		// 从文件读取下一个字节，同时检查是否碰到了起始码
		Buf[pos++] = fgetc (h264bitstream);
		// 从当前位置往前看 3/4 个字节，检测是否是起始码
		// 检测方式：每次读完新字节，检查最后 3 或 4 个字节
		info3 = FindStartCode3(&Buf[pos-4]);   // 检查 Buf[pos-4] 开始的 4 字节是否是 0x00000001
		if(info3 != 1)
			info2 = FindStartCode2(&Buf[pos-3]); // 检查 Buf[pos-3] 开始的 3 字节是否是 0x000001
		StartCodeFound = (info2 == 1 || info3 == 1);
	}

	/*
	 * ---- 第 4 步：找到下一个起始码后的处理 ----
	 *
	 * 当前 Buf 中的内容：
	 *   [起始码_A] [当前NALU数据] [起始码_B（刚找到的）]
	 *   ↑          ↑              ↑
	 *   startcode  pos位置
	 *
	 * 起始码_B 已经被多读进了 Buf，所以：
	 *   - 需要把文件指针回退到起始码_B 的开头（下次调用从此处开始读）
	 *   - NALU 数据长度 = pos + rewind - startcodeprefix_len
	 *     其中 rewind 为负值（-3 或 -4），取决于起始码_B 的类型
	 */
	rewind = (info3 == 1)? -4 : -3;        // 4字节起始码回退4字节，3字节回退3字节

	if (0 != fseek (h264bitstream, rewind, SEEK_CUR)){
		free(Buf);
		printf("GetAnnexbNALU: Cannot fseek in the bit stream file");
	}

	// 计算并拷贝 NALU 数据（跳过前导起始码，截断尾随起始码）
	nalu->len = (pos+rewind)-nalu->startcodeprefix_len;
	memcpy (nalu->buf, &Buf[nalu->startcodeprefix_len], nalu->len);

	// ---- 第 3 步（重复）：解析 NAL Header ----
	nalu->forbidden_bit = nalu->buf[0] & 0x80;       // forbidden_zero_bit: 必须为 0
	nalu->nal_reference_idc = nalu->buf[0] & 0x60;   // nal_ref_idc: 0=可丢弃, 非0=被参考（非0的帧不能随意丢弃）
	nalu->nal_unit_type = (nalu->buf[0]) & 0x1f;     // nal_unit_type: 指明载荷类型（IDR/SPS/PPS/SLICE 等）
	free(Buf);

	return (pos+rewind);                    // 返回本 NALU 在文件中占用的字节数（含起始码）
}

/**
 * ==================== H.264 码流分析主函数 ====================
 * 打开 H.264 文件，循环调用 GetAnnexbNALU 逐个解析 NALU，
 * 并打印每个 NALU 的序号、文件偏移、优先级、类型、长度。
 *
 * @param url    H.264 码流文件的路径
 */
int simplest_h264_parser(char *url){

	NALU_t *n;
	int buffersize=100000;                 // NALU 缓冲区的最大大小（100KB）

	//FILE *myout=fopen("output_log.txt","wb+");
	FILE *myout=stdout;                    // 输出到标准输出（控制台）

	// 打开 H.264 原始码流文件（如 .h264 文件）
	h264bitstream=fopen(url, "rb+");
	if (h264bitstream==NULL){
		printf("Open file error\n");
		return 0;
	}

	// 分配 NALU 结构体内存
	n = (NALU_t*)calloc (1, sizeof (NALU_t));
	if (n == NULL){
		printf("Alloc NALU Error\n");
		return 0;
	}

	// 分配存放 NALU 数据的缓冲区
	n->max_size=buffersize;
	n->buf = (char*)calloc (buffersize, sizeof (char));
	if (n->buf == NULL){
		free (n);
		printf ("AllocNALU: n->buf");
		return 0;
	}

	int data_offset=0;                     // 当前在文件中的偏移量（累计读取的字节数）
	int nal_num=0;                         // NALU 序号计数器
	printf("-----+-------- NALU Table ------+---------+\n");
	printf(" NUM |    POS  |    IDC |  TYPE |   LEN   |\n");
	printf("-----+---------+--------+-------+---------+\n");

	// 主循环：逐个解析 NALU，直到文件末尾
	while(!feof(h264bitstream))
	{
		int data_lenth;
		data_lenth=GetAnnexbNALU(n);       // 提取一个 NALU

		// 查找 NALU 类型的名字字符串
		char type_str[20]={0};
		switch(n->nal_unit_type){
			case NALU_TYPE_SLICE:sprintf(type_str,"SLICE");break;      // P帧/B帧的条带数据
			case NALU_TYPE_DPA:sprintf(type_str,"DPA");break;          // 数据分区 A
			case NALU_TYPE_DPB:sprintf(type_str,"DPB");break;          // 数据分区 B
			case NALU_TYPE_DPC:sprintf(type_str,"DPC");break;          // 数据分区 C
			case NALU_TYPE_IDR:sprintf(type_str,"IDR");break;          // 即时刷新帧（关键帧）
			case NALU_TYPE_SEI:sprintf(type_str,"SEI");break;          // 补充增强信息
			case NALU_TYPE_SPS:sprintf(type_str,"SPS");break;          // 序列参数集
			case NALU_TYPE_PPS:sprintf(type_str,"PPS");break;          // 图像参数集
			case NALU_TYPE_AUD:sprintf(type_str,"AUD");break;          // 访问单元分隔符
			case NALU_TYPE_EOSEQ:sprintf(type_str,"EOSEQ");break;      // 序列结束
			case NALU_TYPE_EOSTREAM:sprintf(type_str,"EOSTREAM");break; // 码流结束
			case NALU_TYPE_FILL:sprintf(type_str,"FILL");break;        // 填充数据
		}
		// 查找优先级名字字符串（nal_reference_idc 右移 5 位得到枚举值 0~3）
		char idc_str[20]={0};
		switch(n->nal_reference_idc>>5){
			case NALU_PRIORITY_DISPOSABLE:sprintf(idc_str,"DISPOS");break;   // 可丢弃
			case NALU_PRIRITY_LOW:sprintf(idc_str,"LOW");break;              // 低
			case NALU_PRIORITY_HIGH:sprintf(idc_str,"HIGH");break;           // 高
			case NALU_PRIORITY_HIGHEST:sprintf(idc_str,"HIGHEST");break;     // 最高
		}

		// 打印 NALU 信息表格的一行
		fprintf(myout,"%5d| %8d| %7s| %6s| %8d|\n",nal_num,data_offset,idc_str,type_str,n->len);

		data_offset=data_offset+data_lenth; // 累加文件偏移

		nal_num++;
	}

	// 释放内存
	if (n){
		if (n->buf){
			free(n->buf);
			n->buf=NULL;
		}
		free (n);
	}
	return 0;
}