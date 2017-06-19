/*
 * LHE Basic encoder
 */

/**
 * @file
 * LHE Basic encoder
 */

#include "avcodec.h"
#include "lhe.h"
#include "internal.h"
#include "put_bits.h"
#include "bytestream.h"
#include "siprdata.h"

/**
 * Adapts hop_1 value depending on last hops. It is used
 * in BASIC LHE and ADVANCED LHE
 */
#define H1_ADAPTATION                                   \
    if (hop_number<=HOP_POS_1 && hop_number>=HOP_NEG_1) \
    {                                                   \
        small_hop=true;                                 \
    } else                                              \
    {                                                   \
        small_hop=false;                                \
    }                                                   \
                                                        \
    if( (small_hop) && (last_small_hop))  {             \
        hop_1=hop_1-1;                                  \
        if (hop_1<MIN_HOP_1) {                          \
            hop_1=MIN_HOP_1;                            \
        }                                               \
                                                        \
    } else {                                            \
        hop_1=MAX_HOP_1;                                \
    }                                                   \
    last_small_hop=small_hop;

/**
 * Lhe Context
 * 
 * @p AVClass *class Pointer to AVClass
 * @p LheBasicPrec prec Caches for LHE
 * @p PutBitContext pb Params for putting bits in a file
 * @p int pr_metrics Print or not print perceptual relevance metrics
 * @p int basic_lhe Basic LHE or Advanced LHE
 */                
typedef struct LheContext {
    AVClass *class;    
    LheBasicPrec prec;
    PutBitContext pb;
    LheProcessing procY;
    LheProcessing procUV;
    LheImage lheY;
    LheImage lheU;
    LheImage lheV;
    uint8_t chroma_factor_width;
    uint8_t chroma_factor_height;
    int pr_metrics;
    int basic_lhe;
    int ql;
    int subsampling_average;
    int dif_frames_count;
} LheContext;

/**
 * Initializes image coder
 * 
 * @param *avctx Pointer to AVCodec context
 */
static av_cold int lhe_encode_init(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    lhe_init_cache(&s->prec);
        
    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
    {
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 2;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV422P) 
    {
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 1;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV444P) 
    {
        s->chroma_factor_width = 1;
        s->chroma_factor_height = 1;
    }
            
    return 0;
}

/**
 * Initializes video coder
 * 
 * @param *avctx Pointer to AVCodec context
 */
static av_cold int mlhe_encode_init(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    lhe_init_cache(&s->prec);
        
    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
    {
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 2;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV422P) 
    {
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 1;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV444P) 
    {
        s->chroma_factor_width = 1;
        s->chroma_factor_height = 1;
    }
        
    s->dif_frames_count = GOP;
    
    return 0;
}


//==================================================================
// AUXILIARY FUNCTIONS
//==================================================================

static void lhe_compute_error_for_psnr (AVCodecContext *avctx, const AVFrame *frame,
                                        uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V) 
{
    
    int error= 0;
    LheContext *s;
    LheProcessing *procY, *procUV;
    LheImage *lheY, *lheU, *lheV;
    
    s = avctx->priv_data;
    procY = &s->procY;
    procUV = &s->procUV;
    
    lheY = &s->lheY;
    lheU = &s->lheU;
    lheV = &s->lheV;

    if(frame->data[0]) {
        for(int y=0; y<procY->height; y++){
            for(int x=0; x<procY->width; x++){
                error = component_original_data_Y[y*frame->linesize[0] + x] - lheY->component_prediction[y*procY->width + x];
                error = abs(error);
                avctx->error[0] += error*error;
            }
        }    
    }
    
    if(frame->data[1]) {
        for(int y=0; y<procUV->height; y++){
            for(int x=0; x<procUV->width; x++){
                error = component_original_data_U[y*frame->linesize[1] + x] - lheU->component_prediction[y*procUV->width + x];
                error = abs(error);
                avctx->error[1] += error*error;
            }
        }    
    }
    
    if(frame->data[2]) {
        for(int y=0; y<procUV->height; y++){
            for(int x=0; x<procUV->width; x++){
                error = component_original_data_V[y*frame->linesize[2] + x] - lheV->component_prediction[y*procUV->width + x];
                error = abs(error);
                avctx->error[2] += error*error;
            }
        }    
    }
}

static void print_json_pr_metrics (LheProcessing *procY, int total_blocks_width, int total_blocks_height) 
{
    int i,j;
    
    av_log (NULL, AV_LOG_PANIC, "[");
        
    for (j=0; j<total_blocks_height+1; j++) 
    {
        for (i=0; i<total_blocks_width+1; i++) 
        {  
            if (i==total_blocks_width && j==total_blocks_height) 
            {
                av_log (NULL, AV_LOG_PANIC, "{\"prx\":%.4f, \"pry\":%.4f}", procY->perceptual_relevance_x[j][i], procY->perceptual_relevance_y[j][i]);
            }
            else 
            {
                av_log (NULL, AV_LOG_PANIC, "{\"prx\":%.4f, \"pry\":%.4f},", procY->perceptual_relevance_x[j][i], procY->perceptual_relevance_y[j][i]);
            }
        }
        
    }
    
    av_log (NULL, AV_LOG_PANIC, "]");   
}

static void print_csv_pr_metrics (LheProcessing *procY, int total_blocks_width, int total_blocks_height) 
{
    int i,j;
            
    for (j=0; j<total_blocks_height+1; j++) 
    {
        for (i=0; i<total_blocks_width+1; i++) 
        {  

            av_log (NULL, AV_LOG_INFO, "%.4f;%.4f;", procY->perceptual_relevance_x[j][i], procY->perceptual_relevance_y[j][i]);

        }
        
        av_log (NULL, AV_LOG_INFO, "\n");

        
    }
    
}



//==================================================================
// BASIC LHE FILE
//==================================================================


/**
 * Returns mask for a given length
 * 
 * @param leng Length
 * @param he_X Parameters for Huffman of that dimension (Y, U or V)
 * @return mask Mask calculated
 */
static int get_mask(uint32_t leng, LheHuffEntry* he_X){
    
    int mask = 0;
    
    mask =  (he_X[HOP_0].code) << (leng-1);

    for (int ll=1; ll<leng; ll++)
    {
        mask = mask | ((!(he_X[HOP_0].code))<<(leng-(ll+1))) ;       
    }
    return mask;
}


/**
 * Gets bits necessary when using RLC for a specific dimension
 * 
 * @param *he_Y Parameters for Huffman of that dimension (Y, U or V)
 * @param image_size Width x Height of that dimension (Y, U or V)
 * @param symbols_X Symbols of that dimension (Y, U or V)
 * @return aux_n_bits Bits needed for that dimension (Y, U or V)
 */
static int get_bits_for_rlc(int image_size, LheHuffEntry* he_X, uint8_t* symbols_X){
    
    int n_bits_dim;
    unsigned int counter_hop_0, counter_bin;
    counter_hop_0 = 0;
    counter_bin   = 0;
    n_bits_dim = 0;
    for (int i=0; i<image_size; i++)
    {    
        //If HOP_0
        if (symbols_X[i] == HOP_0 )
        {
            counter_hop_0++;
            // Less than max_hops HOP_0
            if ( counter_hop_0 <= MAX_HOPS) 
            {
                n_bits_dim += he_X[HOP_0].len;
            }
            // More than 4 HOP_0
            else
            {
                counter_bin = counter_bin + 1;
                if (counter_bin == MAX_NUMBER)
                {
                    n_bits_dim += BIT_NUMBER;
                    counter_bin = 0;
                    counter_hop_0 = MAX_HOPS;
                }
            }
        }
        // If not HOP_0
        else
        {
            // If previous HOP_0
            if (counter_bin != 0)
            {
                n_bits_dim += BIT_NUMBER;

            }
            // If not previous HOP_0
            else
            {
            // If max_hops HOP_0 before, we send 0
                if (counter_hop_0 == MAX_HOPS)
                {
                    n_bits_dim += BIT_NUMBER; 
                }
            }       
            uint32_t code =  he_X[ symbols_X[i] ].code;
            uint32_t leng =  he_X[ symbols_X[i] ].len;
            int mask = get_mask(leng,he_X);
            if (counter_bin != 0)
            {
                    // If HOP_0-00 and HOP_POS_1-01 
                    if( (he_X[HOP_0].len != 1) )
                    {
                        n_bits_dim += leng;
                    }
                    // Lo normal
                    else
                    {
                        n_bits_dim += (leng - 1);  // truncado 
                    }    
            }
            else{
                if (counter_hop_0 == MAX_HOPS)
                {
                    // If HOP_0-00 and HOP_POS_1-01 
                    if( (he_X[HOP_0].len != 1) )
                    {
                        n_bits_dim += leng;
                    }
                    // Lo normal
                    else
                    {
                        n_bits_dim += (leng - 1);  // truncado 
                    }
                }
                else
                {
                    n_bits_dim += he_X[ symbols_X[i] ].len;  // NO truncado 
                }
            }           
            counter_hop_0 = 0;
            counter_bin = 0;
        }
    }
    // After main loop
    if (counter_bin != 0)
    {
        n_bits_dim += BIT_NUMBER; 

    }
    else
    {
        if (counter_hop_0 == MAX_HOPS)
        {
            n_bits_dim += BIT_NUMBER; 
        }
    }  
    return n_bits_dim;
}

/**
 * Generates Huffman for BASIC LHE using RLC
 * 
 * @param *he_Y Parameters for Huffman of luminance signal
 * @param *he_UV Parameters for Huffman of chrominance signals
 * @param *symbols_Y Luminance symbols (or hops)
 * @param *symbols_U Chrominance U symbols (or hops)
 * @param *symbols_V Chrominance V symbols (or hops)
 * @param image_size_Y Width x Height of luminance
 * @param image_size_UV Width x Height of chrominances
 * @return n_bits Number of total bits
 */
static uint64_t lhe_basic_gen_huffman_rlc (LheHuffEntry *he_Y, LheHuffEntry *he_UV,
                                       uint8_t *symbols_Y, uint8_t *symbols_U, uint8_t *symbols_V,
                                       int image_size_Y, int image_size_UV)
{

    int i, ret, n_bits;
    float bpp;
    uint8_t  huffman_lengths_Y[LHE_MAX_HUFF_SIZE_SYMBOLS];
    uint8_t  huffman_lengths_UV[LHE_MAX_HUFF_SIZE_SYMBOLS];
    uint64_t symbol_count_Y[LHE_MAX_HUFF_SIZE_SYMBOLS]     = { 0 };
    uint64_t symbol_count_UV[LHE_MAX_HUFF_SIZE_SYMBOLS]    = { 0 };
    
    //LUMINANCE
    
    //First compute luminance probabilities from model
    for (i=0; i<image_size_Y; i++) {
        symbol_count_Y[symbols_Y[i]]++; //Counts occurrences of different luminance symbols
    }
    
    //Generates Huffman length for luminance signal
    if ((ret = ff_huff_gen_len_table(huffman_lengths_Y, symbol_count_Y, LHE_MAX_HUFF_SIZE_SYMBOLS, 1)) < 0)
        return ret;
    
    //Fills he_Y struct with data
    for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) {
        he_Y[i].len = huffman_lengths_Y[i];
        he_Y[i].count = symbol_count_Y[i];
        he_Y[i].sym = i;
        he_Y[i].code = 1024; //imposible code to initialize
    }
    
    // new
    lhe_generate_huffman_codes(he_Y, LHE_MAX_HUFF_SIZE_SYMBOLS);

    
    //First, compute chrominance probabilities.
    for (i=0; i<image_size_UV; i++) {
        symbol_count_UV[symbols_U[i]]++; //Counts occurrences of different chrominance U symbols
    }
    
    for (i=0; i<image_size_UV; i++) {
        symbol_count_UV[symbols_V[i]]++; //Counts occurrences of different chrominance V symbols
    }

    
     //Generates Huffman length for chrominance signals
    if ((ret = ff_huff_gen_len_table(huffman_lengths_UV, symbol_count_UV, LHE_MAX_HUFF_SIZE_SYMBOLS, 1)) < 0)
        return ret;
    
    //Fills he_UV data
    for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) {
        he_UV[i].len = huffman_lengths_UV[i];
        he_UV[i].count = symbol_count_UV[i];
        he_UV[i].sym = i;
        he_UV[i].code = 1024;
    }

    //Generates chrominance Huffman codes
    // n_bits += lhe_generate_huffman_codes(he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS);
    // bpp = 1.0*n_bits/image_size_Y;
    //av_log (NULL, AV_LOG_INFO, "YUV bpp: %f ", bpp );
    // av_log (NULL, AV_LOG_INFO, "he_Y[%d] \n", he_Y[HOP_0].code);
    
    // new 
    lhe_generate_huffman_codes(he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS);
    n_bits += get_bits_for_rlc(image_size_Y, he_Y, symbols_Y);  
    // av_log (NULL, AV_LOG_INFO, "\n\n\n 1 YUV bpp: %f n_bits: %d \n\n\n", (1.0*n_bits/(image_size_Y) ), n_bits );  
    n_bits += get_bits_for_rlc(image_size_UV, he_UV, symbols_U);
    n_bits += get_bits_for_rlc(image_size_UV, he_UV, symbols_V);

    bpp = 1.0*n_bits/(image_size_Y+image_size_UV+image_size_UV);
    
    // av_log (NULL, AV_LOG_INFO, "\n\n\n 2 YUV bpp: %f n_bits: %d \n\n\n", bpp, n_bits );

    return n_bits;

}


/**
 * Generates Huffman for BASIC LHE
 * 
 * @param *he_Y Parameters for Huffman of luminance signal
 * @param *he_UV Parameters for Huffman of chrominance signals
 * @param *symbols_Y Luminance symbols (or hops)
 * @param *symbols_U Chrominance U symbols (or hops)
 * @param *symbols_V Chrominance V symbols (or hops)
 * @param image_size_Y Width x Height of luminance
 * @param image_size_UV Width x Height of chrominances
 * @return n_bits Number of total bits
 */
static uint64_t lhe_basic_gen_huffman (LheHuffEntry *he_Y, LheHuffEntry *he_UV,
                                       uint8_t *symbols_Y, uint8_t *symbols_U, uint8_t *symbols_V,
                                       int image_size_Y, int image_size_UV)
{
    int i, ret, n_bits;
    float bpp;
    uint8_t  huffman_lengths_Y[LHE_MAX_HUFF_SIZE_SYMBOLS];
    uint8_t  huffman_lengths_UV[LHE_MAX_HUFF_SIZE_SYMBOLS];
    uint64_t symbol_count_Y[LHE_MAX_HUFF_SIZE_SYMBOLS]     = { 0 };
    uint64_t symbol_count_UV[LHE_MAX_HUFF_SIZE_SYMBOLS]    = { 0 };
    
    //LUMINANCE
    
    //First compute luminance probabilities from model
    for (i=0; i<image_size_Y; i++) {
        symbol_count_Y[symbols_Y[i]]++; //Counts occurrences of different luminance symbols
    }
    
    //Generates Huffman length for luminance signal
    if ((ret = ff_huff_gen_len_table(huffman_lengths_Y, symbol_count_Y, LHE_MAX_HUFF_SIZE_SYMBOLS, 1)) < 0)
        return ret;
    
    //Fills he_Y struct with data
    for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) {
        he_Y[i].len = huffman_lengths_Y[i];
        he_Y[i].count = symbol_count_Y[i];
        he_Y[i].sym = i;
        he_Y[i].code = 1024; //imposible code to initialize
    }
    
    //Generates luminance Huffman codes
    n_bits = lhe_generate_huffman_codes(he_Y, LHE_MAX_HUFF_SIZE_SYMBOLS);
    bpp = 1.0*n_bits/image_size_Y;
    
    //av_log (NULL, AV_LOG_INFO, "Y bpp: %f ", bpp );
    
    //CHROMINANCES (same Huffman table for both chrominances)
    
    //First, compute chrominance probabilities.
    for (i=0; i<image_size_UV; i++) {
        symbol_count_UV[symbols_U[i]]++; //Counts occurrences of different chrominance U symbols
    }
    
    for (i=0; i<image_size_UV; i++) {
        symbol_count_UV[symbols_V[i]]++; //Counts occurrences of different chrominance V symbols
    }

    
     //Generates Huffman length for chrominance signals
    if ((ret = ff_huff_gen_len_table(huffman_lengths_UV, symbol_count_UV, LHE_MAX_HUFF_SIZE_SYMBOLS, 1)) < 0)
        return ret;
    
    //Fills he_UV data
    for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) {
        he_UV[i].len = huffman_lengths_UV[i];
        he_UV[i].count = symbol_count_UV[i];
        he_UV[i].sym = i;
        he_UV[i].code = 1024;
    }

    //Generates chrominance Huffman codes
    n_bits += lhe_generate_huffman_codes(he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS);
    bpp = 1.0*n_bits/image_size_Y;

    return n_bits;
    
}


/**
 * Puts bits using RLC logic
 * 
 * @param image_size Width x Height of that dimension (Y, U or V)
 * @param s LHE context
 * @param *he_X Parameters for Huffman of that dimension (Y, U or V)
 * @param *lheX LHE arrays of that dimension (Y, U or V)
 * 
 */
static void sin_rlc(int image_size, LheContext* s, LheHuffEntry* he_X, LheImage* lheX) {
    for (int i=0; i<image_size; i++)
    {
       put_bits(&s->pb, he_X[lheX->hops[i]].len , he_X[lheX->hops[i]].code);
    }
}


/**
 * Puts bits using RLC logic
 * 
 * @param image_size Width x Height of that dimension (Y, U or V)
 * @param s LHE context
 * @param *he_X Parameters for Huffman of that dimension (Y, U or V)
 * @param *lheX LHE arrays of that dimension (Y, U or V)
 * 
 */
static void con_rlc(int image_size, LheContext* s, LheHuffEntry* he_X, LheImage* lheX){
    
    int count__;
    unsigned int counter_hop_0, counter_bin;
    counter_hop_0 = 0;
    counter_bin   = 0;
    count__ = 0;
    

    // for (int i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) {
    //     av_log (NULL, AV_LOG_INFO, "he_X[%d] = %d len %d ------ \n", i, he_X[i].code, he_X[i].len);
    // }
    
    // av_log (NULL, AV_LOG_INFO, "\n\n\n AQUI \n\n\n");

    for (int i=0; i<image_size; i++)
    {    
        // av_log (NULL, AV_LOG_INFO, "%d c_h:%d c_b:%d;", lheX->hops[i], counter_hop_0, counter_bin);
        // av_log (NULL, AV_LOG_INFO, "i:%d %d;", i, lheX->hops[i]);
        count__ = count__ + 1;
        // if((count__+1) % 100 == 0){av_log (NULL, AV_LOG_INFO, "\n");}
        //If HOP_0
        if ( ( lheX->hops[i] ) == HOP_0 )
        {
            counter_hop_0++;
            // Less than max_hops HOP_0
            if ( counter_hop_0 <= MAX_HOPS) 
            {
                put_bits(&s->pb, he_X[HOP_0].len , he_X[HOP_0].code);
            }
            // More than 4 HOP_0
            else
            {
                counter_bin = counter_bin + 1;
                if (counter_bin == MAX_NUMBER)
                {
                    put_bits(&s->pb, BIT_NUMBER, MAX_NUMBER);
                    // av_log (NULL, AV_LOG_INFO, "Mando 1s");
                    counter_bin = 0;
                    counter_hop_0 = MAX_HOPS;
                }
            }
        }
        // If not HOP_0
        else
        {

            // If previous HOP_0
            if (counter_bin != 0)
            {
                put_bits(&s->pb, BIT_NUMBER, counter_bin); 

            }
            // If not previous HOP_0
            else
            {
            // If max_hops HOP_0 before, we send 0
                if (counter_hop_0 == MAX_HOPS)
                {
                    put_bits(&s->pb, BIT_NUMBER, 0); 
                }
            }       
            uint32_t code =  he_X[ lheX->hops[i] ].code;
            uint32_t leng =  he_X[ lheX->hops[i] ].len;
            int mask = get_mask(leng,he_X);
            if (counter_bin != 0)
            {
                    // av_log (NULL, AV_LOG_INFO, "mask:%d code:%d code+mask:%d;",mask,code,code&mask);    
                    // If HOP_0-00 and HOP_POS_1-01 
                    if( (he_X[HOP_0].len != 1) )
                    {
                        put_bits(&s->pb, leng, code);
                    }
                    // Lo normal
                    else
                    {
                        put_bits(&s->pb,leng-1, code&mask);  // truncado 
                    }    
            }
            else{
                if (counter_hop_0 == MAX_HOPS)
                {
                    // av_log (NULL, AV_LOG_INFO, "mask:%d code:%d code+mask:%d;",mask,code,code&mask);    
                    // If HOP_0-00 and HOP_POS_1-01 
                    if( (he_X[HOP_0].len != 1) )
                    {
                        put_bits(&s->pb, leng, code);
                    }
                    // Lo normal
                    else
                    {
                        put_bits(&s->pb,leng-1, code&mask);  // truncado 
                    }
                }
                else
                {
                    put_bits(&s->pb, he_X[ lheX->hops[i] ].len, he_X[ lheX->hops[i] ].code);  // NO truncado 
                    // // probar
                    // put_bits(&s->pb,leng, code);
                }
            }           
            counter_hop_0 = 0;
            counter_bin = 0;
        }
    }



    // After main loop
    if (counter_bin != 0)
    {
        put_bits(&s->pb, BIT_NUMBER, counter_bin); 

    }
    else
    {
        if (counter_hop_0 == MAX_HOPS)
        {
            put_bits(&s->pb, BIT_NUMBER, 0); 
        }
    }  

}


/**
 * Writes BASIC LHE file 
 * 
 * @param *avctx Pointer to AVCodec context
 * @param *pkt Pointer to AVPacket 
 * @param image_size_Y Width x Height of luminance
 * @param image_size_UV Width x Height of chrominances
 * @param total_blocks_width Number of blocks widthwise
 * @param total_blocks_height Number of blocks heightwise
 */
static int lhe_basic_write_file(AVCodecContext *avctx, AVPacket *pkt, 
                                int image_size_Y, int image_size_UV,
                                uint8_t total_blocks_width, uint8_t total_blocks_height) {
  
    uint8_t *buf;
    uint8_t lhe_mode, pixel_format;
    uint64_t n_bits_hops, n_bytes, n_bytes_components, total_blocks;
    
    int i, ret;
        
    LheContext *s;
    LheProcessing *procY;
    LheImage *lheY;
    LheImage *lheU;
    LheImage *lheV;
    
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE_SYMBOLS]; //Struct for luminance Huffman data
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE_SYMBOLS]; //Struct for chrominance Huffman data
    
    struct timeval before , after;
    
    s = avctx->priv_data;
    procY = &s->procY;
    lheY = &s->lheY;
    lheU = &s->lheU;
    lheV = &s->lheV;
    
    total_blocks = total_blocks_height * total_blocks_width; //Number of blocks in the image
    
    gettimeofday(&before , NULL);

    //Generates Huffman
    //n_bits_hops = lhe_basic_gen_huffman (he_Y, he_UV, (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops, image_size_Y, image_size_UV);      
    //// RLC-MODE
    n_bits_hops = lhe_basic_gen_huffman_rlc (he_Y, he_UV, (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops, image_size_Y, image_size_UV);   
    

    // //Imprimir
    // for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) {
    //     av_log (NULL, AV_LOG_INFO, "he_Y[%d] = %d len %d -- \n", i, he_Y[i].code, he_Y[i].len);
    // }
    
    // for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) {
    //     av_log (NULL, AV_LOG_INFO, "he_UV[%d] = %d len %d -- \n", i, he_UV[i].code, he_UV[i].len);
    // }

    n_bytes_components = n_bits_hops/8;          
       
    //File size
    n_bytes = sizeof(lhe_mode) + sizeof(pixel_format) + //Lhe mode and pixel format
              + sizeof(procY->width) + sizeof(procY->height) //width and height
              + sizeof(total_blocks_height) + sizeof(total_blocks_width) //Number of blocks heightwise and widthwise
              + total_blocks * (sizeof(*lheY->first_color_block) + sizeof(*lheU->first_color_block) + sizeof(*lheV->first_color_block)) 
              + LHE_HUFFMAN_TABLE_BYTES_SYMBOLS + //huffman table
              + n_bytes_components; //components
              

    //av_log (NULL, AV_LOG_INFO, "YUV+Header bpp: %f \n ", (n_bytes*8.0)/image_size_Y);
              
    //ff_alloc_packet2 reserves n_bytes of memory
    if ((ret = ff_alloc_packet2(avctx, pkt, n_bytes, 0)) < 0)
        return ret;

    buf = pkt->data; //Pointer to write buffer
    init_put_bits(&s->pb, buf, n_bytes);
      
    //Lhe mode byte
    if(OPENMP_FLAGS == CONFIG_OPENMP) 
    {
        lhe_mode = PARAREL_BASIC_LHE; 
    } 
    else 
    {
        lhe_mode = SEQUENTIAL_BASIC_LHE; 
    }
    
    put_bits(&s->pb, LHE_MODE_SIZE_BITS, lhe_mode);
    
    //Pixel format byte
    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
    {
        pixel_format = LHE_YUV420;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV422P) 
    {
        pixel_format = LHE_YUV422;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV444P) 
    {
        pixel_format = LHE_YUV444;
    }
    
    put_bits(&s->pb, PIXEL_FMT_SIZE_BITS, pixel_format);
   
    //save width and height
    put_bits32(&s->pb, procY->width);
    put_bits32(&s->pb, procY->height);

    //Save first component of each signal 
    for (i=0; i<total_blocks; i++) 
    {
        put_bits(&s->pb, FIRST_COLOR_SIZE_BITS, lheY->first_color_block[i]);
    }
    
      for (i=0; i<total_blocks; i++) 
    {
        put_bits(&s->pb, FIRST_COLOR_SIZE_BITS, lheU->first_color_block[i]);
    }
    
      for (i=0; i<total_blocks; i++) 
    {
        put_bits(&s->pb, FIRST_COLOR_SIZE_BITS, lheV->first_color_block[i]);
    }
    
    //Write Huffman tables
    for (i=0; i<LHE_MAX_HUFF_SIZE_SYMBOLS; i++)
    {
        if (he_Y[i].len==255) he_Y[i].len=15;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS_SYMBOLS, he_Y[i].len);
    }
    
    for (i=0; i<LHE_MAX_HUFF_SIZE_SYMBOLS; i++)
    {
        if (he_UV[i].len==255) he_UV[i].len=15;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS_SYMBOLS, he_UV[i].len);
    }   
    
    // sin_rlc(image_size_Y, s, he_Y, lheY);
    // sin_rlc(image_size_UV, s, he_UV, lheU);
    // sin_rlc(image_size_UV, s, he_UV, lheV);


    // for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) {
    //     av_log (NULL, AV_LOG_INFO, "he_Y[%d] = %d len %d ---- \n", i, he_Y[i].code, he_Y[i].len);
    // }
    
    // for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) {
    //     av_log (NULL, AV_LOG_INFO, "he_UV[%d] = %d len %d ---- \n", i, he_UV[i].code, he_UV[i].len);
    // }

    /// RLC-MODE
    con_rlc(image_size_Y, s, he_Y, lheY);
    con_rlc(image_size_UV, s, he_UV, lheU);
    con_rlc(image_size_UV, s, he_UV, lheV);
    
    flush_put_bits(&s->pb);
        
    gettimeofday(&after , NULL);

    //av_log(NULL, AV_LOG_INFO, "LHE WriteTime %.0lf ", time_diff(before , after));
    
    return n_bytes;
}


//==================================================================
// ADVANCED LHE FILE
//==================================================================

/**
 * Generates Huffman for ADVANCED LHE 
 * 
 * @param *he_Y Parameters for Huffman of luminance signal
 * @param *he_UV Parameters for Huffman of chrominance signals
 * @param *procY Parameters for luminance LHE processing
 * @param *procUV Parameters for chrominance LHE processing
 * @param *lheY Luminance LHE arrays Advanced block parameters for luminance signal
 * @param *lheU Chrominance U LHE arrays Advanced block parameters for chrominance signals
 * @param *lheV Chrominance V LHE arrays
 * @param total_blocks_width Number of blocks widthwise
 * @param total_blocks_height Number of blocks heightwise
 * @return n_bits Number of total bits
 */
static uint64_t lhe_advanced_gen_huffman (LheHuffEntry *he_Y, LheHuffEntry *he_UV,
                                          LheProcessing *procY, LheProcessing *procUV,
                                          LheImage *lheY, LheImage *lheU, LheImage *lheV,
                                          uint32_t total_blocks_width, uint32_t total_blocks_height)
{
    int i, ret, n_bits;
    float bpp;
    uint8_t  huffman_lengths_Y[LHE_MAX_HUFF_SIZE_SYMBOLS];
    uint8_t  huffman_lengths_UV[LHE_MAX_HUFF_SIZE_SYMBOLS];
    uint64_t symbol_count_Y[LHE_MAX_HUFF_SIZE_SYMBOLS]     = { 0 };
    uint64_t symbol_count_UV[LHE_MAX_HUFF_SIZE_SYMBOLS]    = { 0 };
    
    uint32_t xini_Y, xini_UV, xfin_downsampled_Y, xfin_downsampled_UV, yini_Y, yini_UV, yfin_downsampled_Y, yfin_downsampled_UV;

    //LUMINANCE
    //First compute luminance probabilities from model taking into account different image blocks
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {

            xini_Y = procY->basic_block[block_y][block_x].x_ini;
            yini_Y = procY->basic_block[block_y][block_x].y_ini;
            
            xfin_downsampled_Y = procY->advanced_block[block_y][block_x].x_fin_downsampled;          
            yfin_downsampled_Y = procY->advanced_block[block_y][block_x].y_fin_downsampled;
               
            xini_UV = procUV->basic_block[block_y][block_x].x_ini;
            yini_UV = procUV->basic_block[block_y][block_x].y_ini;
            
            xfin_downsampled_UV = procUV->advanced_block[block_y][block_x].x_fin_downsampled;
            yfin_downsampled_UV = procUV->advanced_block[block_y][block_x].y_fin_downsampled;
             
            //LUMINANCE
            for (int y=yini_Y; y<yfin_downsampled_Y; y++) 
            {
                for (int x=xini_Y; x<xfin_downsampled_Y; x++) {
                    symbol_count_Y[lheY->hops[y*procY->width + x]]++;  //Generates Huffman length for luminance signal               
                }
            }  
      
            //CHROMINANCE
            for (int y=yini_UV; y<yfin_downsampled_UV; y++) 
            {
                for (int x=xini_UV; x<xfin_downsampled_UV; x++) {
                    symbol_count_UV[lheU->hops[y*procUV->width + x]]++;  //Generates Huffman length for chrominance U signal
                    symbol_count_UV[lheV->hops[y*procUV->width + x]]++;  //Generates Huffman length for chrominance V signal
                }
            } 
        }
    }
    

    //LUMINANCE
    //Generates Huffman length for luminance
    if ((ret = ff_huff_gen_len_table(huffman_lengths_Y, symbol_count_Y, LHE_MAX_HUFF_SIZE_SYMBOLS, 1)) < 0)
        return ret;
    
    //Fills he_Y struct with data
    for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) 
    {
        he_Y[i].len = huffman_lengths_Y[i];
        he_Y[i].count = symbol_count_Y[i];
        he_Y[i].sym = i;
        he_Y[i].code = 1024; //imposible code to initialize   
    }
    
    //Generates luminance Huffman codes
    n_bits = lhe_generate_huffman_codes(he_Y, LHE_MAX_HUFF_SIZE_SYMBOLS);
    bpp = 1.0*n_bits/(procY->width*procY->height);
    
    //av_log (NULL, AV_LOG_INFO, "Y bpp: %f ",bpp );
    //av_log (NULL, AV_LOG_INFO, "%f; ",bpp );
    
    //CHROMINANCES
    //Generate Huffman length chrominance (same Huffman table for both chrominances)
    if ((ret = ff_huff_gen_len_table(huffman_lengths_UV, symbol_count_UV, LHE_MAX_HUFF_SIZE_SYMBOLS, 1)) < 0)
        return ret;
    
    //Fills he_UV struct with data
    for (i = 0; i < LHE_MAX_HUFF_SIZE_SYMBOLS; i++) 
    {
        he_UV[i].len = huffman_lengths_UV[i];
        he_UV[i].count = symbol_count_UV[i];
        he_UV[i].sym = i;
        he_UV[i].code = 1024;      
    }

    //Generates chrominance Huffman codes
    n_bits += lhe_generate_huffman_codes(he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS);
    bpp = 1.0*n_bits/(procY->width*procY->height);

    //av_log (NULL, AV_LOG_INFO, "YUV bpp: %f ", bpp);
    
    //Returns total bits
    return n_bits; 
}

/**
 * Translates Perceptual relevance values into perceptual relevance interval number 
 * to save it on advanced lhe file.
 * 
 *    Interval   -  Quant - Interval number
 * [0.0, 0.125)  -  0.0   -         0
 * [0.125, 0.25) -  0.125 -         1
 * [0.25, 0.5)   -  0.25  -         2
 * [0.5, 0.75)   -  0.5   -         3
 * [0.75, 1]     -  1.0   -         4
 * 
 * @param perceptual_relevance perceptual relevance value to translate
 */
static uint8_t lhe_advanced_translate_pr_into_mesh (float perceptual_relevance) 
{
    uint8_t pr_interval;
    if (perceptual_relevance == PR_QUANT_0) 
    {
        pr_interval = PR_INTERVAL_0;
    } 
    else if (perceptual_relevance == PR_QUANT_1) 
    {
        pr_interval = PR_INTERVAL_1;
    }
    else if (perceptual_relevance == PR_QUANT_2) 
    {
        pr_interval = PR_INTERVAL_2;
    }
    else if (perceptual_relevance == PR_QUANT_3) 
    {
        pr_interval = PR_INTERVAL_3;
    }
    else if (perceptual_relevance == PR_QUANT_5) 
    {
        pr_interval = PR_INTERVAL_4;
    }
   
    return pr_interval;
}

/**
 * Generates Huffman codes for Perceptual Relevance mesh
 * 
 * @param *he_mesh Mesh Huffman parameters
 * @param **perceptual_relevance_x perceptual relevance values in x coordinate
 * @param **perceptual_relevance_y perceptual relevance values in y coordinate
 * @param total_blocks_width number of blocks widthwise
 * @param total_blocks_height number of blocks heightwise
 */
static uint64_t lhe_advanced_gen_huffman_mesh (LheHuffEntry *he_mesh, 
                                               float **perceptual_relevance_x, float **perceptual_relevance_y,                                          
                                               uint32_t total_blocks_width, uint32_t total_blocks_height)
{
    int ret, pr_interval_x, pr_interval_y;
    uint64_t n_bits;
    uint8_t  huffman_lengths_mesh [LHE_MAX_HUFF_SIZE_MESH];
    uint64_t symbol_count_mesh [LHE_MAX_HUFF_SIZE_MESH]     = { 0 };
    
    //Compute probabilities from model
    for (int block_y=0; block_y<total_blocks_height+1; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width+1; block_x++) 
        { 
            pr_interval_x = lhe_advanced_translate_pr_into_mesh(perceptual_relevance_x[block_y][block_x]);
            pr_interval_y = lhe_advanced_translate_pr_into_mesh(perceptual_relevance_y[block_y][block_x]);
            symbol_count_mesh[pr_interval_x]++;
            symbol_count_mesh[pr_interval_y]++;

        }
    }

    //Generates Huffman length for mesh
    if ((ret = ff_huff_gen_len_table(huffman_lengths_mesh, symbol_count_mesh, LHE_MAX_HUFF_SIZE_MESH, 1)) < 0)
        return ret;
    
    //Fills he_mesh struct with data
    for (int i = 0; i < LHE_MAX_HUFF_SIZE_MESH; i++) {
        he_mesh[i].len = huffman_lengths_mesh[i];
        he_mesh[i].count = symbol_count_mesh[i];
        he_mesh[i].sym = i;
        he_mesh[i].code = 1024; //imposible code to initialize   
    }
    
    //Generates mesh Huffman codes
    n_bits = lhe_generate_huffman_codes(he_mesh, LHE_MAX_HUFF_SIZE_MESH);
  
    return n_bits; 
}

/**
 * Writes ADVANCED LHE file 
 * 
 * @param *avctx Pointer to AVCodec context
 * @param *pkt Pointer to AVPacket 
 * @param image_size_Y Width x Height of luminance
 * @param image_size_UV Width x Height of chrominances
 * @param total_blocks_width Number of blocks widthwise
 * @param total_blocks_height Number of blocks heightwise
 */
static int lhe_advanced_write_file(AVCodecContext *avctx, AVPacket *pkt, 
                                   uint32_t image_size_Y, uint32_t image_size_UV, 
                                   uint8_t total_blocks_width, uint8_t total_blocks_height) 
{
  
    uint8_t *buf;
    uint8_t lhe_mode, pixel_format, pr_interval;
    uint64_t n_bits_hops, n_bits_mesh, n_bytes, n_bytes_components, n_bytes_mesh, total_blocks;
    uint32_t xini_Y, xfin_downsampled_Y, yini_Y, yfin_downsampled_Y, xini_UV, xfin_downsampled_UV, yini_UV, yfin_downsampled_UV; 
    uint64_t pix;
    int i, ret;
            
    LheContext *s;
    LheProcessing *procY;
    LheProcessing *procUV;
    LheImage *lheY;
    LheImage *lheU;
    LheImage *lheV;
    
    LheHuffEntry he_mesh[LHE_MAX_HUFF_SIZE_MESH]; //Struct for mesh Huffman data
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE_SYMBOLS]; //Struct for luminance Huffman data
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE_SYMBOLS]; //Struct for chrominance Huffman data
    
    struct timeval before , after;
    
    s = avctx->priv_data;
    procY = &s->procY;
    procUV = &s->procUV;
    lheY = &s->lheY;
    lheU = &s->lheU;
    lheV = &s->lheV;
        
    total_blocks = total_blocks_height * total_blocks_width;
    
    gettimeofday(&before , NULL);

    //Generates HUffman
    n_bits_mesh = lhe_advanced_gen_huffman_mesh (he_mesh, 
                                                 procY->perceptual_relevance_x, procY->perceptual_relevance_y,                                          
                                                 total_blocks_width, total_blocks_height);
  
    n_bytes_mesh = (n_bits_mesh / 8) + 1;
    
    n_bits_hops = lhe_advanced_gen_huffman (he_Y, he_UV, 
                                            procY, procUV, lheY, lheU, lheV,
                                            total_blocks_width, total_blocks_height);
     
    n_bytes_components = (n_bits_hops/8) + 1;     
    
    
    //File size
    n_bytes = sizeof(lhe_mode) + sizeof (pixel_format) +
              + sizeof(procY->width) + sizeof(procY->height) //width and height
              + total_blocks * (sizeof(*lheY->first_color_block) + sizeof(*lheU->first_color_block) + sizeof(*lheV->first_color_block)) //first pixel blocks array value
              + sizeof (s->ql) + //quality level
              + LHE_HUFFMAN_TABLE_BYTES_MESH
              + LHE_HUFFMAN_TABLE_BYTES_SYMBOLS + //huffman table
              + n_bytes_mesh 
              + n_bytes_components;
              
    //av_log (NULL, AV_LOG_INFO, "YUV+Header bpp: %f \n", (n_bytes*8.0)/image_size_Y);
              
    //ff_alloc_packet2 reserves n_bytes of memory
    if ((ret = ff_alloc_packet2(avctx, pkt, n_bytes, 0)) < 0)
        return ret;

    buf = pkt->data; //Pointer to write buffer
    
    init_put_bits(&s->pb, buf, n_bytes);
   
    //LHE Mode
    lhe_mode = ADVANCED_LHE; 
    
    //Lhe mode byte
    put_bits(&s->pb, LHE_MODE_SIZE_BITS, lhe_mode);    
    
    //Pixel format byte
    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
    {
        pixel_format = LHE_YUV420;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV422P) 
    {
        pixel_format = LHE_YUV422;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV444P) 
    {
        pixel_format = LHE_YUV444;
    }
    
    put_bits(&s->pb, PIXEL_FMT_SIZE_BITS, pixel_format);    
        
    //save width and height
    put_bits32(&s->pb, procY->width);    
    put_bits32(&s->pb, procY->height);    


    //Save first pixel for each block
    for (i=0; i<total_blocks; i++) 
    {
        put_bits(&s->pb, FIRST_COLOR_SIZE_BITS, lheY->first_color_block[i]);  
    }
    
      for (i=0; i<total_blocks; i++) 
    {
        put_bits(&s->pb, FIRST_COLOR_SIZE_BITS, lheU->first_color_block[i]);  
    }
    
      for (i=0; i<total_blocks; i++) 
    {
        put_bits(&s->pb, FIRST_COLOR_SIZE_BITS, lheV->first_color_block[i]);  
    }
         
    //Write Huffman tables 
    for (i=0; i<LHE_MAX_HUFF_SIZE_SYMBOLS; i++)
    {
        if (he_Y[i].len==255) he_Y[i].len=LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS_SYMBOLS, he_Y[i].len);
    }
    
    for (i=0; i<LHE_MAX_HUFF_SIZE_SYMBOLS; i++)
    {
        if (he_UV[i].len==255) he_UV[i].len=LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS_SYMBOLS, he_UV[i].len);
    } 
    
    for (i=0; i<LHE_MAX_HUFF_SIZE_MESH; i++)
    {
        if (he_mesh[i].len==255) he_mesh[i].len=LHE_HUFFMAN_NO_OCCURRENCES_MESH;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS_MESH, he_mesh[i].len);
    }
    
    //Advanced LHE quality level
    put_bits(&s->pb, QL_SIZE_BITS, s->ql);    
    
    //Write mesh. First PRX, then PRY because it eases the decoding task
    //Perceptual Relevance x intervals
    for (int block_y=0; block_y<total_blocks_height+1; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width+1; block_x++) 
        { 
            pr_interval = lhe_advanced_translate_pr_into_mesh(procY->perceptual_relevance_x[block_y][block_x]);
            put_bits(&s->pb, he_mesh[pr_interval].len, he_mesh[pr_interval].code);
        }
    }
    
     //Perceptual relevance y intervals
    for (int block_y=0; block_y<total_blocks_height+1; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width+1; block_x++) 
        { 
            pr_interval = lhe_advanced_translate_pr_into_mesh(procY->perceptual_relevance_y[block_y][block_x]);
            put_bits(&s->pb, he_mesh[pr_interval].len, he_mesh[pr_interval].code);

        }
    }
    
    //Write hops
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {
            xini_Y = procY->basic_block[block_y][block_x].x_ini;
            yini_Y = procY->basic_block[block_y][block_x].y_ini;
            
            xfin_downsampled_Y = procY->advanced_block[block_y][block_x].x_fin_downsampled;          
            yfin_downsampled_Y = procY->advanced_block[block_y][block_x].y_fin_downsampled;
               
            xini_UV = procUV->basic_block[block_y][block_x].x_ini;
            yini_UV = procUV->basic_block[block_y][block_x].y_ini;
            
            xfin_downsampled_UV = procUV->advanced_block[block_y][block_x].x_fin_downsampled;
            yfin_downsampled_UV = procUV->advanced_block[block_y][block_x].y_fin_downsampled;
         
            //LUMINANCE
            for (int y=yini_Y; y<yfin_downsampled_Y; y++) 
            {
                for (int x=xini_Y; x<xfin_downsampled_Y; x++) {
                    pix = y*procY->width + x;
                    put_bits(&s->pb, he_Y[lheY->hops[pix]].len , he_Y[lheY->hops[pix]].code);
                }
            }
            
            //CHROMINANCE U
            for (int y=yini_UV; y<yfin_downsampled_UV; y++) 
            {
                for (int x=xini_UV; x<xfin_downsampled_UV; x++) {
                    pix = y*procUV->width + x;
                    put_bits(&s->pb, he_UV[lheU->hops[pix]].len , he_UV[lheU->hops[pix]].code);
                }
            }
            
            //CHROMINANCE_V
            for (int y=yini_UV; y<yfin_downsampled_UV; y++) 
            {
                for (int x=xini_UV; x<xfin_downsampled_UV; x++) {
                    pix = y*procUV->width + x;
                    put_bits(&s->pb, he_UV[lheV->hops[pix]].len , he_UV[lheV->hops[pix]].code);
                }
            }
        }
    }
    
    flush_put_bits(&s->pb);
    gettimeofday(&after , NULL);

    //av_log(NULL, AV_LOG_INFO, "LHE WriteTime %.0lf \n", time_diff(before , after));
    
    return n_bytes;
}
                             

//==================================================================
// BASIC LHE FUNCTIONS
//==================================================================
/**
 * Encodes one hop per pixel sequentially
 * 
 * @param *prec Pointer to LHE precalculated parameters
 * @param *proc LHE processing parameters.
 * @param *lhe LHE image arrays
 * @param *component_original_data original image
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 */
static void lhe_basic_encode_one_hop_per_pixel (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe,
                                                uint8_t *component_original_data, int linesize)
{      

    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t predicted_component, hop_1, hop_number, original_color, r_max;
    int pix, pix_original_data, dif_line, x, y;

    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_component=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size   
    pix_original_data = 0;
    x = 0;
    y = 0;
    original_color=0;              // original color
    r_max=PARAM_R;
    
    dif_line = linesize - proc->width;
    
    for (y=0; y < proc->height; y++)  
    {
        for (x=0; x < proc->width; x++)  
        {    
            original_color = component_original_data[pix_original_data];    
        
            if (x==0 && y==0) //First pixel
            {
                predicted_component=original_color;
                lhe->first_color_block[0]=original_color; //Save first component (needed in lhe file)
            }
            else if (y == 0) //First row
            {
                predicted_component=lhe->component_prediction[pix-1];                
            }
            else if (x == 0) //First column
            {
                predicted_component=lhe->component_prediction[pix-proc->width];
                last_small_hop=false;
                hop_1=START_HOP_1;  
            } 
            else if (x == proc->width -1) //Last column
            {
                predicted_component=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix-proc->width])>>1;                               
            }
            else //Rest of the image
            {
                predicted_component = (lhe->component_prediction[pix-1]+lhe->component_prediction[pix+1-proc->width])>>1; 
            }
            
            
            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_component];            
            lhe->component_prediction[pix]=prec -> prec_luminance[predicted_component][r_max][hop_1][hop_number];  
            lhe->hops[pix]= hop_number;
                        
            H1_ADAPTATION;
            pix++;   
            pix_original_data++;

        }
        pix_original_data+=dif_line;            
    }    
    
}

/**
 * Encodes one hop per pixel in a block
 * 
 * @param *prec Pointer to LHE precalculated parameters
 * @param *proc LHE processing parameters.
 * @param *lhe LHE image arrays
 * @param *component_original_data original image
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param total_blocks_width number of blocks widthwise
 * @param total_blocks_height number of blocks heightwise
 * @param block_x block in x coordinate to encode
 * @param block_y block in y coordinate to encode
 */
static void lhe_basic_encode_one_hop_per_pixel_block (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe, uint8_t *component_original_data,
                                                      int linesize, uint32_t total_blocks_width, uint32_t total_blocks_height, int block_x, int block_y)
{      
    
    //Hops computation.
    int xini, xfin, yini, yfin;
    bool small_hop, last_small_hop;
    uint8_t predicted_component, hop_1, hop_number, original_color, r_max;
    int pix, pix_original_data, dif_line, dif_pix ,num_block;
    
    num_block = block_y * total_blocks_width + block_x;
    
    //ORIGINAL IMAGE
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin = proc->basic_block[block_y][block_x].x_fin;
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin = proc->basic_block[block_y][block_x].y_fin;
   
    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_component=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max=PARAM_R;
    
    pix = yini*proc->width + xini;
    pix_original_data = yini*linesize + xini;
    
    dif_pix = proc->width - xfin + xini; //amount of pixels we have to jump in sps image
    dif_line = linesize - xfin + xini; //amount of pixels we have to jump in original image
    
    for (int y=yini; y < yfin; y++)  {
        for (int x=xini; x < xfin; x++)  {
            
            original_color = component_original_data[pix_original_data]; //This can't be pix because ffmpeg adds empty memory slots. 

            //prediction of signal (predicted_component) , based on pixel's coordinates 
            //----------------------------------------------------------
                        
            if (x == xini && y==yini) //First pixel block
            {
                predicted_component=original_color;//first pixel always is perfectly predicted! :-)  
                lhe->first_color_block[num_block] = original_color;
            } 
            else if (y == yini) //First row 
            {
                predicted_component=lhe->component_prediction[pix-1];
            } 
            else if (x == xini) //First column
            {
                predicted_component=lhe->component_prediction[pix-proc->width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin - 1) //Last column
            {
                predicted_component=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix-proc->width])>>1;                               
            } 
            else //Rest of the block
            {
                predicted_component=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix+1-proc->width])>>1;     
            }


            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_component]; 
            lhe->hops[pix]= hop_number;
            lhe->component_prediction[pix]=prec -> prec_luminance[predicted_component][r_max][hop_1][hop_number];


            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
            pix_original_data++; //we jumped number of samples needed
        }//for x
        pix+=dif_pix; 
        pix_original_data+=dif_line;
    }//for y     
}

/**
 * Calls methods to encode sequentially
 */
static void lhe_basic_encode_frame_sequential (LheContext *s, const AVFrame *frame, 
                                               uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V)
{
    //Luminance
    lhe_basic_encode_one_hop_per_pixel(&s->prec, &s->procY, &s->lheY, component_original_data_Y, frame->linesize[0]); 

    //Crominance U
    lhe_basic_encode_one_hop_per_pixel(&s->prec, &s->procUV, &s->lheU, component_original_data_U, frame->linesize[1]); 

    //Crominance V
    lhe_basic_encode_one_hop_per_pixel(&s->prec, &s->procUV, &s->lheV, component_original_data_V, frame->linesize[2]); 
  
}

/**
 * Call methods to encode parallel
 */
static void lhe_basic_encode_frame_pararell (LheContext *s, const AVFrame *frame,
                                             uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V,
                                             int total_blocks_width, int total_blocks_height)
{        
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++)      
    {  
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {
            lhe_calculate_block_coordinates (&s->procY, &s->procUV,
                                             total_blocks_width, total_blocks_height,
                                             block_x, block_y);

            //Luminance
            lhe_basic_encode_one_hop_per_pixel_block(&s->prec, &s->procY, &s->lheY, component_original_data_Y,     
                                                     frame->linesize[0], total_blocks_width, total_blocks_height, block_x, block_y);

            
            //Crominance U
            lhe_basic_encode_one_hop_per_pixel_block(&s->prec, &s->procUV, &s->lheU, component_original_data_U, 
                                                     frame->linesize[1], total_blocks_width, total_blocks_height, block_x, block_y);

            //Crominance V
            lhe_basic_encode_one_hop_per_pixel_block(&s->prec, &s->procUV, &s->lheV, component_original_data_V,       
                                                     frame->linesize[2], total_blocks_width, total_blocks_height, block_x, block_y);
                                                     
                                               
        }
    }  
}


//==================================================================
// ADVANCED LHE FUNCTIONS
//==================================================================
/**
 * Performs Basic LHE and computes PRx
 * 
 * @param *prec Pointer to LHE precalculated parameters
 * @param *component_original_data original image
 * @param xini initial x coordinate for pr block
 * @param xfin final x coordinate for pr block
 * @param yini initial y coordinate for pr block
 * @param yfin initial y coordinate for pr block
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param sps_ratio_height indicates how often an image sample is taken heightwise to encode
 */
static float lhe_advanced_compute_prx (LheBasicPrec *prec,
                                       uint8_t *component_original_data, 
                                       int xini, int xfin, int yini, int yfin, 
                                       int linesize, 
                                       uint8_t sps_ratio_height)
{          
    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t predicted_component, hop_1, hop_number, original_color, r_max;
    int pix, pix_original_data, dif_pix_original_data;
    
    int hx, count_hx, weight;
    uint8_t last_prediction, last_hop_number;
    float prx;
    
    hx = 0;
    count_hx = 0;
    prx =0;
       
    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_component=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max=PARAM_R;
    
    pix = 0;
    pix_original_data = yini*linesize + xini;
    dif_pix_original_data = sps_ratio_height * linesize - xfin + xini; //amount of pixels we have to jump in original image
    
    for (int y=yini; y < yfin; y+=sps_ratio_height)  {
        
        last_hop_number = HOP_0;
        
        for (int x=xini; x < xfin; x++)  {
                        
            original_color = component_original_data[pix_original_data]; //This can't be pix because ffmpeg adds empty memory slots. 

            //prediction of signal (predicted_component) , based on pixel's coordinates 
            //----------------------------------------------------------
                        
            if (x == xini) //First column
            {
                last_small_hop=false;
                hop_1=START_HOP_1;
                predicted_component=original_color;//first pixel always is perfectly predicted! :-)  

            } else {
                predicted_component=last_prediction;
            }

            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_component];          
            last_prediction = prec -> prec_luminance[predicted_component][r_max][hop_1][hop_number];

            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;
            
            //lets go for the next pixel
            //--------------------------
            pix++;
            pix_original_data++; //we jumped number of samples needed
            
            //Hops count for PRx
            //--------------------------
            if (hop_number == HOP_0) continue;
            
            if (hop_number == HOP_POS_4 || hop_number == HOP_NEG_4 || (hop_number > HOP_0 && last_hop_number < HOP_0) || (hop_number < HOP_0 && last_hop_number > HOP_0)) {
    
                // WEIGHT will be only abs(-4...0...4)
                weight = hop_number - HOP_0; 
                
                if (weight < 0) weight = -weight;
                
                hx += weight;
                count_hx++;
            }      
            
            last_hop_number = hop_number;   
          
        }//for x

        pix_original_data+=dif_pix_original_data;
    }//for y    
    
    
    //Computes PRX
    //--------------------------
    
    if (count_hx == 0) 
    {
        prx = 0;      
    } else 
    {
        prx = (1.0 * hx) / (count_hx * PR_HMAX);
    }
    
    return prx;
}


/**
 * Performs Basic LHE and computes PRy
 * 
 * @param *prec Pointer to LHE precalculated parameters
 * @param *component_original_data original image
 * @param xini initial x coordinate for pr block
 * @param xfin final x coordinate for pr block
 * @param yini initial y coordinate for pr block
 * @param yfin initial y coordinate for pr block
 * @param height image height
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param sps_ratio_width indicates how often an image sample is taken widthwise to encode
 */
static float lhe_advanced_compute_pry (LheBasicPrec *prec, LheProcessing *proc,
                                       uint8_t *component_original_data, 
                                       int xini, int xfin, int yini, int yfin, 
                                       int linesize, 
                                       uint8_t sps_ratio_width)
{      
    
    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t predicted_component, hop_1, hop_number, original_color, r_max;
    int pix, pix_original_data;
    
    int hy, count_hy, weight;
    uint8_t last_prediction, top_hop_number;
    float pry;
    
    hy = 0;
    count_hy = 0;
    pry = 0;
       
    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_component=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max=PARAM_R;

    pix = 0;
    pix_original_data = yini*linesize + xini;
        
    for (int x=xini; x < xfin; x+=sps_ratio_width)  {
        
        pix_original_data = yini * linesize + x;
        top_hop_number = HOP_0;
        
        for (int y=yini; y < yfin; y++)  {
            
            original_color = component_original_data[pix_original_data]; 

            //prediction of signal (predicted_component) , based on pixel's coordinates 
            //----------------------------------------------------------                   
            if (y == yini) //First column
            {
                last_small_hop=false;
                hop_1=START_HOP_1;
                predicted_component=original_color;//first pixel always is perfectly predicted! :-)  
            } else {
                predicted_component=last_prediction;
            }
            

            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_component]; 
            last_prediction = prec -> prec_luminance[predicted_component][r_max][hop_1][hop_number];

            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
            pix_original_data+=linesize; //we jump number of samples needed
            
            //Hops count for PRx
            //--------------------------
            if (hop_number == HOP_0) continue;
            
            if (hop_number == HOP_POS_4 || hop_number == HOP_NEG_4 || (hop_number > HOP_0 && top_hop_number < HOP_0) || (hop_number < HOP_0 && top_hop_number > HOP_0)) {
    
                // WEIGHT will be only abs(-4...0...4)
                weight = hop_number - HOP_0; 
                
                if (weight < 0) weight = -weight;
                
                hy += weight;
                count_hy++;
            }      
            
            top_hop_number = hop_number;   
        }//for x
    }//for y     
    
    //Computes PRX
    //--------------------------
    
    if (count_hy == 0) 
    {
        pry = 0;      
    } else 
    {
        pry = (1.0 * hy) / (count_hy * PR_HMAX);
    }
    
    return pry;
}

/**
 * 
 * Histogram expansion
 * PR quantization
 * 
 * @param *proc LHE processing params
 * @param prx perceptual relevance in x of the block
 * @param pry perceptual relevance in y of the block
 * @param block_x Block x index
 * @param block_y Block y index
 */
static void lhe_advanced_pr_histogram_expansion_and_quantization (LheProcessing *proc,
                                                                  float prx, float pry,
                                                                  int block_x, int block_y) 
{
    //PR HISTOGRAM EXPANSION
    prx = (prx-PR_MIN) / PR_DIF;
            
    //PR QUANTIZATION
    if (prx < PR_QUANT_1) {
        prx = PR_QUANT_0;
    } else if (prx < PR_QUANT_2) {
        prx = PR_QUANT_1;
    } else if (prx < PR_QUANT_3) {
        prx = PR_QUANT_2;
    } else if (prx < PR_QUANT_4) {
        prx = PR_QUANT_3;
    } else {
        prx = PR_QUANT_5;
    }       

    proc->perceptual_relevance_x[block_y][block_x] = prx;
        
 
    //PR HISTOGRAM EXPANSION
    pry = (pry-PR_MIN) / PR_DIF;
    
    //PR QUANTIZATION
    if (pry < PR_QUANT_1) {
        pry = PR_QUANT_0;
    } else if (pry < PR_QUANT_2) {
        pry = PR_QUANT_1;
    } else if (pry < PR_QUANT_3) {
        pry = PR_QUANT_2;
    } else if (pry < PR_QUANT_4) {
        pry = PR_QUANT_3;
    } else {
        pry = PR_QUANT_5;
    }
    
    proc->perceptual_relevance_y[block_y][block_x] = pry;
}


/**
 * Computes perceptual relevance. 
 * 
 * @param *s LHE Context
 * @param *component_original_data_Y original image data
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param total_blocks_width total blocks widthwise
 * @param total_blocks_height total blocks heightwise
 */
static void lhe_advanced_compute_perceptual_relevance (LheContext *s, uint8_t *component_original_data_Y,                                           
                                                       int linesize, uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    
    int xini, xfin, yini, yfin, xini_pr_block, xfin_pr_block, yini_pr_block, yfin_pr_block;
    uint32_t block_width, block_height;
    float prx, pry;
    
    LheProcessing *proc;
    LheBasicPrec *prec;
    
    proc = &s->procY;
    prec = &s->prec;
    
    block_width = proc->theoretical_block_width;
    block_height = proc->theoretical_block_height;
    
    //#pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height+1; block_y++)      
    {  
        for (int block_x=0; block_x<total_blocks_width+1; block_x++) 
        {      
            //First LHE Block coordinates
            xini = block_x * block_width;
            xfin = xini +  block_width;

            yini = block_y * block_height;
            yfin = yini + block_height;
            
            //PR Blocks coordinates 
            xini_pr_block = xini - (block_width >>1); 
            
            if (xini_pr_block < 0) 
            {
                xini_pr_block = 0;
            }
            
            xfin_pr_block = xfin - (block_width>>1);
            
            if (block_x == total_blocks_width) 
            {
                xfin_pr_block = proc->width;
            }    
            
            yini_pr_block = yini - (block_height>>1);
            
            if (yini_pr_block < 0) 
            {
                yini_pr_block = 0;
            }
            
            yfin_pr_block = yfin - (block_height>>1);
            
            if (block_y == total_blocks_height)
            {
                yfin_pr_block = proc->height;
            }

            prx = lhe_advanced_compute_prx (prec,
                                            component_original_data_Y, 
                                            xini_pr_block, xfin_pr_block, yini_pr_block, yfin_pr_block, 
                                            linesize, 
                                            SPS_FACTOR);
            
            pry = lhe_advanced_compute_pry (prec, proc,
                                            component_original_data_Y, 
                                            xini_pr_block, xfin_pr_block, yini_pr_block, yfin_pr_block, 
                                            linesize, 
                                            SPS_FACTOR);
 
            //Calls method to compute perceptual relevance using calculated coordinates 
            lhe_advanced_pr_histogram_expansion_and_quantization (proc, prx, pry, block_x, block_y) ;  
        }
    }
}

/**
 * Downsamples image in x coordinate with different resolution along the block. 
 * Final sample is average of ppp samples that it represents 
 * 
 * @param *proc LHE processing parameters
 * @param *component_original_data original image
 * @param *intermediate_downsample intermediate downsampled image in x coordinate
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_horizontal_downsample_average (LheProcessing *proc, uint8_t *component_original_data, uint8_t *intermediate_downsample_image,
                                                        int linesize, int block_x, int block_y) 
{
    uint32_t block_height, downsampled_x_side, xini, xdown_prev, xdown_fin, xfin_downsampled, yini, yfin;
    float xdown_prev_float, xdown_fin_float;
    float gradient, gradient_0, gradient_1, ppp_x, ppp_0, ppp_1, ppp_2, ppp_3;
    float component_float, percent;
    uint8_t component;
    
    block_height = proc->basic_block[block_y][block_x].block_height;
    downsampled_x_side = proc->advanced_block[block_y][block_x].downsampled_x_side;

    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled;
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin = proc->basic_block[block_y][block_x].y_fin;  
        
    ppp_0=proc->advanced_block[block_y][block_x].ppp_x[TOP_LEFT_CORNER];
    ppp_1=proc->advanced_block[block_y][block_x].ppp_x[TOP_RIGHT_CORNER];
    ppp_2=proc->advanced_block[block_y][block_x].ppp_x[BOT_LEFT_CORNER];
    ppp_3=proc->advanced_block[block_y][block_x].ppp_x[BOT_RIGHT_CORNER];
    
    //gradient PPPx side a
    gradient_0=(ppp_2-ppp_0)/(block_height-1.0);   
    //gradient PPPx side b
    gradient_1=(ppp_3-ppp_1)/(block_height-1.0);
    
    for (int y=yini; y<yfin; y++)
    {            
        gradient=(ppp_1-ppp_0)/(downsampled_x_side-1.0); 

        ppp_x=ppp_0;
        
        xdown_prev_float = xini;
        xdown_prev = xini;
        xdown_fin_float = xini + ppp_x;
        

        for (int x=xini; x<xfin_downsampled; x++)
        {
            xdown_fin = xdown_fin_float;
            
            component_float = 0;
            percent = (1-(xdown_prev_float-xdown_prev));
            
            component_float +=percent*component_original_data[y*linesize+xdown_prev];          
            
            for (int i=xdown_prev+1; i<xdown_fin; i++)
            {
                component_float += component_original_data[y*linesize+i];               
            }
    
            if (xdown_fin_float>xdown_fin)
            {
                percent = xdown_fin_float-xdown_fin;
                component_float += percent * component_original_data[y*linesize+xdown_fin];
            }
                                 
            component_float = component_float / ppp_x;
            
            if (component_float <= 0) component_float = 1;
            if (component_float > 255) component_float = 255;
            
            component = component_float + 0.5;
            
            
            intermediate_downsample_image[y*proc->width+x] = component;
            
            ppp_x+=gradient;
            xdown_prev = xdown_fin;
            xdown_prev_float = xdown_fin_float;
            xdown_fin_float+=ppp_x;
        }//x

        ppp_0+=gradient_0;
        ppp_1+=gradient_1;

    }//y
}

/**
 * Downsamples image in y coordinate with different resolution along the block. 
 * Final sample is average of ppp samples that it represents 
 * 
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays
 * @param *intermediate_downsample_image downsampled image in x coordinate
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_vertical_downsample_average (LheProcessing *proc, LheImage *lhe, uint8_t *intermediate_downsample_image, int block_x, int block_y) 
{
    
    float ppp_y, ppp_0, ppp_1, ppp_2, ppp_3, gradient, gradient_0, gradient_1;
    uint32_t block_width, downsampled_y_side, xini, xfin_downsampled, yini, ydown_prev, ydown_fin, yfin_downsampled;
    float ydown_prev_float, ydown_fin_float;
    float component_float, percent;
    uint8_t component;
  
    downsampled_y_side = proc->advanced_block[block_y][block_x].downsampled_y_side;
    block_width = proc->basic_block[block_y][block_x].block_width;
            
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled; //Vertical downsampling is performed after horizontal down. x coord has been already down.  
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin_downsampled = proc->advanced_block[block_y][block_x].y_fin_downsampled;

    ppp_0=proc->advanced_block[block_y][block_x].ppp_y[TOP_LEFT_CORNER];
    ppp_1=proc->advanced_block[block_y][block_x].ppp_y[TOP_RIGHT_CORNER];
    ppp_2=proc->advanced_block[block_y][block_x].ppp_y[BOT_LEFT_CORNER];
    ppp_3=proc->advanced_block[block_y][block_x].ppp_y[BOT_RIGHT_CORNER];

    //gradient PPPy side c
    gradient_0=(ppp_1-ppp_0)/(block_width-1.0);    
    //gradient PPPy side d
    gradient_1=(ppp_3-ppp_2)/(block_width-1.0);
      
    for (int x=xini; x < xfin_downsampled;x++)
    {
        gradient=(ppp_2-ppp_0)/(downsampled_y_side-1.0);
        ppp_y=ppp_0; 

        ydown_prev = yini;
        ydown_prev_float = yini;
        ydown_fin_float = yini + ppp_y;
        
        for (int y=yini; y < yfin_downsampled; y++)
        {
            ydown_fin = ydown_fin_float;
            
            component_float = 0;
            percent = (1-(ydown_prev_float-ydown_prev));
          
            component_float += percent * intermediate_downsample_image[ydown_prev*proc->width+x];
           
            for (int i=ydown_prev+1; i<ydown_fin; i++)
            {
                component_float += intermediate_downsample_image[i*proc->width+x];
            }
            
            if (ydown_fin_float>ydown_fin)
            {
                percent = ydown_fin_float-ydown_fin;
                component_float += percent *intermediate_downsample_image[ydown_fin*proc->width+x];
            }
            
            component_float = component_float / ppp_y;
            
            if (component_float <= 0) component_float = 1;
            if (component_float > 255) component_float = 255;
            
            component = component_float + 0.5;
            
            lhe->downsampled_image[y*proc->width+x] = component;
                                   
            ppp_y+=gradient;
            ydown_prev = ydown_fin;
            ydown_prev_float = ydown_fin_float;
            ydown_fin_float+=ppp_y;
        }//ysc
        ppp_0+=gradient_0;
        ppp_2+=gradient_1;

    }//x
    
}

static void lhe_advanced_downsample_sps (LheProcessing *proc, LheImage *lhe, uint8_t *component_original_data, int linesize, int block_x, int block_y) 
{
    float pppx_0, pppx_1, pppx_2, pppx_3, pppy_0, pppy_1, pppy_2, pppy_3, pppx, pppx_a, pppx_b, pppy_a, pppy_b;
    float gradx_a, grady_a, gradx_b, grady_b, gradx, grady;

    uint32_t downsampled_x_side, downsampled_y_side, xini, xfin, xfin_downsampled, yini, yfin, y_sc;
    float ya_ini, yb_ini, x_float, y_float, xa;
    uint32_t width, height;
    int x, y;
    
    width = proc->width;
    height = proc->height;
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin = proc->basic_block[block_y][block_x].x_fin;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled;

    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin = proc->basic_block[block_y][block_x].y_fin;  

    pppx_0=proc->advanced_block[block_y][block_x].ppp_x[TOP_LEFT_CORNER];
    pppx_1=proc->advanced_block[block_y][block_x].ppp_x[TOP_RIGHT_CORNER];
    pppx_2=proc->advanced_block[block_y][block_x].ppp_x[BOT_LEFT_CORNER];
    pppx_3=proc->advanced_block[block_y][block_x].ppp_x[BOT_RIGHT_CORNER];

    pppy_0=proc->advanced_block[block_y][block_x].ppp_y[TOP_LEFT_CORNER];
    pppy_1=proc->advanced_block[block_y][block_x].ppp_y[TOP_RIGHT_CORNER];
    pppy_2=proc->advanced_block[block_y][block_x].ppp_y[BOT_LEFT_CORNER];
    pppy_3=proc->advanced_block[block_y][block_x].ppp_y[BOT_RIGHT_CORNER];
    
    downsampled_x_side = proc->advanced_block[block_y][block_x].downsampled_x_side;
    downsampled_y_side = proc->advanced_block[block_y][block_x].downsampled_y_side;
    
    //gradient side a
    gradx_a=(pppx_2 - pppx_0)/(downsampled_y_side-1.0);
    grady_a=(pppy_2 - pppy_0)/(downsampled_y_side-1.0);

    //gradient side b
    gradx_b=(pppx_3 - pppx_1)/(downsampled_y_side-1.0);
    grady_b=(pppy_3 - pppy_1)/(downsampled_y_side-1.0);

    //initialization of ppp at side a and ppp at side b
    pppx_a=pppx_0;
    pppx_b=pppx_1;
    pppy_a=pppy_0;
    pppy_b=pppy_1;
            
    y_sc=yini;
    ya_ini=(uint32_t)(yini+pppy_a/2.0);
    yb_ini=(uint32_t)(yini+pppy_b/2.0);
    
    
    for (float ya=ya_ini,yb=yb_ini;ya<yfin;ya+=pppy_a,yb+=pppy_b)
    {
        gradx=(pppx_b-pppx_a)/(downsampled_x_side-1.0); 
        
        grady=(yb-ya)/(downsampled_x_side-1.0); 
    
        //initialization of pppx at start of scanline
        pppx=pppx_a;
        
        xa=xini+pppx/2.0;
        
        //dominio original
        y_float=ya;
        x_float=xa;
                        
        for (int x_sc=xini;x_sc<xfin_downsampled;x_sc++)
        {
            x = x_float;
            y= y_float;
            
            if (x>width-1) x=width-1;
            if (y>height-1) y=height-1;
            if (x<0) x=0;
            if (y<0) y=0;

            lhe->downsampled_image[y_sc*width+x_sc] = component_original_data[y*linesize+x];

            x_float+=pppx;
            y_float+=grady;
 
            pppx+=gradx;
        }//x
        pppx_a+=gradx_a;
        pppx_b+=gradx_b;
        pppy_a+=grady_a;
        pppy_b+=grady_b;
        y_sc++; 
        if (y_sc>=height) break;

    }//y
}

/**
 * Downsamples image in x coordinate with different resolution along the block. 
 * Samples are taken using sps with different cadence depending on ppp (pixels per pixel)
 * 
 * @param *proc LHE processing parameters
 * @param *component_original_data original imagage
 * @param *intermediate_downsample_image downsampled image in x coordinate
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_horizontal_downsample_sps (LheProcessing *proc, uint8_t *component_original_data, uint8_t *intermediate_downsample_image,
                                                    int linesize, int block_x, int block_y) 
{
    uint32_t block_height, downsampled_x_side, xini, xdown, xfin_downsampled, yini, yfin;
    float xdown_float;
    float gradient, gradient_0, gradient_1, ppp_x, ppp_0, ppp_1, ppp_2, ppp_3;

    
    block_height = proc->basic_block[block_y][block_x].block_height;
    downsampled_x_side = proc->advanced_block[block_y][block_x].downsampled_x_side;

    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled;
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin = proc->basic_block[block_y][block_x].y_fin;  
        
    ppp_0=proc->advanced_block[block_y][block_x].ppp_x[TOP_LEFT_CORNER];
    ppp_1=proc->advanced_block[block_y][block_x].ppp_x[TOP_RIGHT_CORNER];
    ppp_2=proc->advanced_block[block_y][block_x].ppp_x[BOT_LEFT_CORNER];
    ppp_3=proc->advanced_block[block_y][block_x].ppp_x[BOT_RIGHT_CORNER];
    
    //gradient PPPx side a
    gradient_0=(ppp_2-ppp_0)/(block_height-1.0);   
    //gradient PPPx side b
    gradient_1=(ppp_3-ppp_1)/(block_height-1.0);

    for (int y=yini; y<yfin; y++)
    {        
        gradient=(ppp_1-ppp_0)/(downsampled_x_side-1.0); 

        ppp_x=ppp_0;
        xdown_float=xini + (ppp_x/2.0) - 0.5;

        for (int x=xini; x<xfin_downsampled; x++)
        {
            xdown = xdown_float;
                      
            intermediate_downsample_image[y*proc->width+x]=component_original_data[y*linesize+xdown];

            ppp_x+=gradient;
            xdown_float+=ppp_x;

        }//x

        ppp_0+=gradient_0;
        ppp_1+=gradient_1;

    }//y
}

/**
 * Downsamples image in y coordinate with different resolution along the block. 
 * Samples are taken using sps with different cadence depending on ppp (pixels per pixel)
 * 
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays
 * @param *intermediate_downsample_image downsampled image in x coordinate
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_vertical_downsample_sps (LheProcessing *proc, LheImage *lhe, uint8_t *intermediate_downsample_image, int block_x, int block_y) 
{
    
    float ppp_y, ppp_0, ppp_1, ppp_2, ppp_3, gradient, gradient_0, gradient_1;
    uint32_t block_width, downsampled_y_side, xini, xfin_downsampled, yini, ydown, yfin_downsampled;
    float ydown_float;
    
    block_width = proc->basic_block[block_y][block_x].block_width;
    downsampled_y_side = proc->advanced_block[block_y][block_x].downsampled_y_side;
    
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled; //Vertical downsampling is performed after horizontal down. x coord has been already down.  
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin_downsampled = proc->advanced_block[block_y][block_x].y_fin_downsampled;

    ppp_0=proc->advanced_block[block_y][block_x].ppp_y[TOP_LEFT_CORNER];
    ppp_1=proc->advanced_block[block_y][block_x].ppp_y[TOP_RIGHT_CORNER];
    ppp_2=proc->advanced_block[block_y][block_x].ppp_y[BOT_LEFT_CORNER];
    ppp_3=proc->advanced_block[block_y][block_x].ppp_y[BOT_RIGHT_CORNER];

    //gradient PPPy side c
    gradient_0=(ppp_1-ppp_0)/(block_width-1.0);    
    //gradient PPPy side d
    gradient_1=(ppp_3-ppp_2)/(block_width-1.0);
      
    for (int x=xini; x < xfin_downsampled;x++)
    {
        gradient=(ppp_2-ppp_0)/(downsampled_y_side-1.0);
        ppp_y=ppp_0; 

        ydown_float=yini + (ppp_y/2.0); 
        
        for (int y=yini; y < yfin_downsampled; y++)
        {
            ydown = ydown_float - 0.5;
            lhe->downsampled_image[y*proc->width+x]=intermediate_downsample_image[ydown*proc->width+x];
      
            ppp_y+=gradient;
            ydown_float+=ppp_y;
        }//ysc
        ppp_0+=gradient_0;
        ppp_2+=gradient_1;

    }//x
    
}

/**
 * Encodes block in Advanced LHE (downsampled image)
 * 
 * @param *prec Pointer to LHE precalculated data
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays
 * @param total_blocks_width number of blocks widthwise
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_encode_block (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe,
                                       int total_blocks_width, int block_x, int block_y)
{      
    
    //Hops computation.
    int xini, xfin_downsampled, yini, yfin_downsampled;
    bool small_hop, last_small_hop;
    uint8_t predicted_luminance, hop_1, hop_number, original_color, r_max;
    int pix, dif_pix, num_block;
            
    num_block = block_y * total_blocks_width + block_x;
    
    //DOWNSAMPLED IMAGE
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin_downsampled = proc->advanced_block[block_y][block_x].y_fin_downsampled;
    
    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max = PARAM_R;
    
    pix = yini*proc->width + xini;       
    dif_pix = proc->width - xfin_downsampled + xini;    

    for (int y=yini; y < yfin_downsampled; y++)  {
        for (int x=xini; x < xfin_downsampled; x++)  {
              
            original_color = lhe->downsampled_image[pix]; //This can't be pix because ffmpeg adds empty memory slots. 

            //prediction of signal (predicted_luminance) , based on pixel's coordinates 
            //----------------------------------------------------------
                        
            if (x == xini && y==yini) 
            {
                predicted_luminance=original_color;//first pixel always is perfectly predicted! :-)  
                lhe->first_color_block[num_block] = original_color;
            }

            else if (y == yini) 
            {
                predicted_luminance=lhe->component_prediction[pix-1];
            } 
            else if (x == xini) 
            {
                predicted_luminance=lhe->component_prediction[pix-proc->width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin_downsampled -1) 
            {
                predicted_luminance=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix-proc->width])>>1;    
            } 
            else 
            {
                predicted_luminance=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix+1-proc->width])>>1;     
            }
             
            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_luminance]; 
            lhe->hops[pix]= hop_number;        
            lhe->component_prediction[pix]=prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop_number];         
            
            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
        }//for x
        pix+=dif_pix;
    }//for y    
}

/**
 * LHE advanced encoding
 * 
 * PR to PPP conversion
 * PPP to rectangle shape
 * Elastic Downsampling
 */
static float lhe_advanced_encode (LheContext *s, const AVFrame *frame,  
                                  uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V,
                                  uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    float compression_factor;
    uint8_t *intermediate_downsample_Y, *intermediate_downsample_U, *intermediate_downsample_V;
    uint32_t image_size_Y, image_size_UV, ppp_max_theoric;
            
    image_size_Y = (&s->procY)->width * (&s->procY)->height;
    image_size_UV = (&s->procUV)->width * (&s->procUV)->height;
    
    ppp_max_theoric = (&s->procY)->theoretical_block_width/SIDE_MIN;
    compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->ql];

    intermediate_downsample_Y = av_calloc (image_size_Y, sizeof(uint8_t));
    intermediate_downsample_U = av_calloc (image_size_UV, sizeof(uint8_t));
    intermediate_downsample_V = av_calloc (image_size_UV, sizeof(uint8_t));
    
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++)      
    {  
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {
                lhe_calculate_block_coordinates (&s->procY, &s->procUV,
                                                 total_blocks_width, total_blocks_height,
                                                 block_x, block_y);                                                                                                            
        }
    }
 
    lhe_advanced_compute_perceptual_relevance (s, component_original_data_Y, frame->linesize[0], total_blocks_width,  total_blocks_height);

    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {              
            lhe_advanced_perceptual_relevance_to_ppp(&s->procY, &s->procUV, compression_factor, ppp_max_theoric, block_x, block_y);
            
            lhe_advanced_ppp_side_to_rectangle_shape (&s->procY, ppp_max_theoric, block_x, block_y);        
            lhe_advanced_ppp_side_to_rectangle_shape (&s->procUV, ppp_max_theoric, block_x, block_y);
            
            if (s->subsampling_average)
            {
                //LUMINANCE
                //Downsamples using component original data         
                lhe_advanced_horizontal_downsample_average (&s->procY, component_original_data_Y, intermediate_downsample_Y,
                                                            frame->linesize[0], block_x, block_y);
                                                        

                lhe_advanced_vertical_downsample_average (&s->procY, &s->lheY, intermediate_downsample_Y, block_x, block_y);
                
                //CHROMINANCE U
                lhe_advanced_horizontal_downsample_average (&s->procUV,component_original_data_U, intermediate_downsample_U,
                                                            frame->linesize[1], block_x, block_y);
                                                        

                lhe_advanced_vertical_downsample_average (&s->procUV, &s->lheU, intermediate_downsample_U, block_x, block_y);
                
                //CHROMINANCE_V
                lhe_advanced_horizontal_downsample_average (&s->procUV, component_original_data_V, intermediate_downsample_V,
                                                            frame->linesize[2], block_x, block_y);
                                                        

                lhe_advanced_vertical_downsample_average (&s->procUV, &s->lheV, intermediate_downsample_V, block_x, block_y);
                 
            } else {
                //LUMINANCE
                lhe_advanced_horizontal_downsample_sps (&s->procY, component_original_data_Y, intermediate_downsample_Y,
                                                        frame->linesize[0], block_x, block_y);
                                                        
                lhe_advanced_vertical_downsample_sps (&s->procY, &s->lheY, intermediate_downsample_Y, block_x, block_y);
                
                //CHROMINANCE U
                lhe_advanced_horizontal_downsample_sps (&s->procUV,component_original_data_U, intermediate_downsample_U,
                                                        frame->linesize[1], block_x, block_y);
                                                        
                lhe_advanced_vertical_downsample_sps (&s->procUV, &s->lheU, intermediate_downsample_U, block_x, block_y);
                
                //CHROMINANCE_V
                lhe_advanced_horizontal_downsample_sps (&s->procUV, component_original_data_V, intermediate_downsample_V,
                                                        frame->linesize[2], block_x, block_y);
                
                lhe_advanced_vertical_downsample_sps (&s->procUV, &s->lheV, intermediate_downsample_V, block_x, block_y);               
            }

            //LUMINANCE                                     
            //Encode downsampled blocks                          
            lhe_advanced_encode_block (&s->prec, &s->procY, &s->lheY, total_blocks_width, block_x,  block_y);          
                                       
            //CHROMINANCE U                                    
            lhe_advanced_encode_block (&s->prec, &s->procUV, &s->lheU, total_blocks_width, block_x,  block_y);
                              
            //CHROMINANCE V                                                                                      
            lhe_advanced_encode_block (&s->prec, &s->procUV, &s->lheV, total_blocks_width, block_x,  block_y);         
        }
    } 
    
    av_free(intermediate_downsample_Y);
    av_free(intermediate_downsample_U);
    av_free(intermediate_downsample_V);
   
    return compression_factor;
}

//==================================================================
// LHE VIDEO FUNCTIONS
//==================================================================
/**
 * Calculates last frame data taking into account the error commited when quantizing delta
 * 
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays
 * @param *delta_prediction differential quantized frame
 * @param *last_downsampled_data last downsampled frame 
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param block_x block x index
 * @param block_y block y index
 */
static void mlhe_calculate_error (LheProcessing *proc, LheImage *lhe,
                                  uint8_t *delta_prediction, uint8_t *last_downsampled_data, 
                                  uint32_t linesize, uint32_t block_x, uint32_t block_y)
{
    int pix, error_delta;
    uint32_t xini, xfin, yini, yfin;
    
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin = proc->advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin = proc->advanced_block[block_y][block_x].y_fin_downsampled;
    
    #pragma omp parallel for
    for (int y=yini; y<yfin; y++) {
        for (int x=xini; x<xfin; x++) {
            pix = y*proc->width + x;
            
            error_delta = last_downsampled_data[pix] + (delta_prediction[pix] - 128) * 2;
            if (error_delta > 255) error_delta = 255;
            if (error_delta < 0) error_delta = 1;
            
            lhe->downsampled_error_image[pix] = error_delta;
        }
    }
}

/**
 * Encodes block in Advanced LHE (downsampled image)
 * 
 * @param *prec Pointer to LHE precalculated data
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays
 * @param *delta original differential frame
 * @param *delta_prediction quantized differential frame
 * @param total_blocks_width number of blocks widthwise
 * @param block_x block x index
 * @param block_y block y index
 */
static void mlhe_encode_delta (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe,
                               uint8_t *delta_prediction, uint8_t *adapted_last_downsampled_image, 
                               int total_blocks_width, int block_x, int block_y)
{      
    
    //Hops computation.
    int xini, xfin_downsampled, yini, yfin_downsampled;
    bool small_hop, last_small_hop;
    uint8_t predicted_luminance, hop_1, hop_number, r_max;
    int pix, dif_pix, num_block, original_color;
            
    num_block = block_y * total_blocks_width + block_x;
    
    //DOWNSAMPLED IMAGE
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin_downsampled = proc->advanced_block[block_y][block_x].y_fin_downsampled;
    
    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max = PARAM_R;
    
    pix = yini*proc->width + xini;       
    dif_pix = proc->width - xfin_downsampled + xini;    

    for (int y=yini; y < yfin_downsampled; y++)  {
        for (int x=xini; x < xfin_downsampled; x++)  {
              
            original_color = (lhe->downsampled_image[pix] - adapted_last_downsampled_image[pix]) / 2 + 128;
            if (original_color > 255) 
            {
                original_color = 255;
            }
            else if (original_color < 0) 
            {
                original_color = 1;
            }

            //prediction of signal (predicted_luminance) , based on pixel's coordinates 
            //----------------------------------------------------------
                        
            if (x == xini && y==yini) 
            {
                predicted_luminance=original_color;//first pixel always is perfectly predicted! :-)  
                lhe->first_color_block[num_block] = original_color;
            }

            else if (y == yini) 
            {
                predicted_luminance=delta_prediction[pix-1];
            } 
            else if (x == xini) 
            {
                predicted_luminance=delta_prediction[pix-proc->width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin_downsampled -1) 
            {
                predicted_luminance=(delta_prediction[pix-1]+delta_prediction[pix-proc->width])>>1;    
            } 
            else 
            {
                predicted_luminance=(delta_prediction[pix-1]+delta_prediction[pix+1-proc->width])>>1;     
            }
             
            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_luminance]; 
            lhe->hops[pix]= hop_number;
            delta_prediction[pix]=prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop_number];
            
            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
        }//for x
        pix+=dif_pix;
    }//for y    
}

/**
 * Encodes differential frame
 * 
 * @param *s LHE Context
 * @param *frame Frame parameters
 * @param *component_original_data_Y luminance original data
 * @param *component_original_data_U chrominance u original data
 * @param *component_original_data_V chrominance v original data
 * @param total_blocks_width number of blocks widthwise
 * @param total_blocks_height number of blocks heightwise
 */
static void mlhe_delta_frame_encode (LheContext *s, const AVFrame *frame,                               
                                     uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V,
                                     uint32_t total_blocks_width, uint32_t total_blocks_height) {
    
    float compression_factor;
    uint8_t *intermediate_downsample_Y, *intermediate_downsample_U, *intermediate_downsample_V;
    uint32_t image_size_Y, image_size_UV, ppp_max_theoric;
    uint8_t *intermediate_adapted_downsampled_data_Y, *intermediate_adapted_downsampled_data_U, *intermediate_adapted_downsampled_data_V;
    uint8_t *adapted_downsampled_data_Y, *adapted_downsampled_data_U, *adapted_downsampled_data_V;
    uint8_t *delta_prediction_Y, *delta_prediction_U, *delta_prediction_V;

    image_size_Y = (&s->procY)->width * (&s->procY)->height;
    image_size_UV = (&s->procUV)->width * (&s->procUV)->height;
    
    ppp_max_theoric = (&s->procY)->theoretical_block_width/SIDE_MIN;
    compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->ql];

    intermediate_downsample_Y = av_calloc (image_size_Y, sizeof(uint8_t));
    intermediate_downsample_U = av_calloc (image_size_UV, sizeof(uint8_t));
    intermediate_downsample_V = av_calloc (image_size_UV, sizeof(uint8_t));

    intermediate_adapted_downsampled_data_Y = av_calloc(image_size_Y, sizeof(uint8_t));  
    intermediate_adapted_downsampled_data_U = av_calloc(image_size_UV, sizeof(uint8_t)); 
    intermediate_adapted_downsampled_data_V = av_calloc(image_size_UV, sizeof(uint8_t)); 
    
    adapted_downsampled_data_Y = av_calloc(image_size_Y, sizeof(uint8_t));  
    adapted_downsampled_data_U = av_calloc(image_size_UV, sizeof(uint8_t)); 
    adapted_downsampled_data_V = av_calloc(image_size_UV, sizeof(uint8_t)); 
    
    delta_prediction_Y = av_calloc(image_size_Y, sizeof(uint8_t));  
    delta_prediction_U = av_calloc(image_size_UV, sizeof(uint8_t)); 
    delta_prediction_V = av_calloc(image_size_UV, sizeof(uint8_t));   
    
    lhe_advanced_compute_perceptual_relevance (s, component_original_data_Y, frame->linesize[0],
                                               total_blocks_width,  total_blocks_height);
    
     
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {                        

            lhe_advanced_perceptual_relevance_to_ppp(&s->procY, &s->procUV,
                                                     compression_factor, ppp_max_theoric, 
                                                     block_x, block_y);
            
            
            lhe_advanced_ppp_side_to_rectangle_shape (&s->procY, ppp_max_theoric, 
                                                      block_x, block_y);        
            lhe_advanced_ppp_side_to_rectangle_shape (&s->procUV, ppp_max_theoric, 
                                                      block_x, block_y);
            
            //LUMINANCE
            lhe_advanced_horizontal_downsample_sps (&s->procY, component_original_data_Y, 
                                                    intermediate_downsample_Y,
                                                    frame->linesize[0], block_x, block_y);
                                                    

            lhe_advanced_vertical_downsample_sps (&s->procY, &s->lheY, intermediate_downsample_Y, 
                                                  block_x, block_y);

            
            mlhe_adapt_downsampled_data_resolution (&s->procY, &s->lheY, 
                                                    intermediate_adapted_downsampled_data_Y, 
                                                    adapted_downsampled_data_Y,
                                                    block_x, block_y);
         
            mlhe_encode_delta (&s->prec, &s->procY, &s->lheY, delta_prediction_Y,
                               adapted_downsampled_data_Y, total_blocks_width, block_x,  block_y);
            
            mlhe_calculate_error (&s->procY, &s->lheY,
                                  delta_prediction_Y, adapted_downsampled_data_Y,  
                                  frame->linesize[0], block_x, block_y);
            
            //CHROMINANCE U
            lhe_advanced_horizontal_downsample_sps (&s->procUV,component_original_data_U, 
                                                    intermediate_downsample_U, 
                                                    frame->linesize[1], block_x, block_y);
                                                    

            lhe_advanced_vertical_downsample_sps (&s->procUV, &s->lheU, intermediate_downsample_U, 
                                                  block_x, block_y);
            
            
            mlhe_adapt_downsampled_data_resolution (&s->procUV, &s->lheU,
                                                    intermediate_adapted_downsampled_data_U, 
                                                    adapted_downsampled_data_U,
                                                    block_x, block_y);
            
            mlhe_encode_delta (&s->prec, &s->procUV, &s->lheU, delta_prediction_U, 
                               adapted_downsampled_data_U, total_blocks_width, block_x,  block_y);
                   
            
            mlhe_calculate_error (&s->procUV, &s->lheU,
                                  delta_prediction_U, adapted_downsampled_data_U,  
                                  frame->linesize[1], block_x, block_y);
            
            //CHROMINANCE_V
            lhe_advanced_horizontal_downsample_sps (&s->procUV, component_original_data_V, 
                                                    intermediate_downsample_V,
                                                    frame->linesize[2], block_x, block_y);
            
            lhe_advanced_vertical_downsample_sps (&s->procUV, &s->lheV, intermediate_downsample_V, 
                                                  block_x, block_y);    
                        
            
            mlhe_adapt_downsampled_data_resolution (&s->procUV, &s->lheV,
                                                    intermediate_adapted_downsampled_data_V, 
                                                    adapted_downsampled_data_V,
                                                    block_x, block_y);
                                                                                     
             mlhe_encode_delta (&s->prec, &s->procUV, &s->lheV, delta_prediction_V, 
                                adapted_downsampled_data_V, total_blocks_width, block_x,  block_y);
            
             mlhe_calculate_error (&s->procUV, &s->lheV,
                                   delta_prediction_V, adapted_downsampled_data_V,  
                                   frame->linesize[2], block_x, block_y);
                                       
        }
    }   
    
    av_free(intermediate_downsample_Y); 
    av_free(intermediate_downsample_U); 
    av_free(intermediate_downsample_V); 

    av_free(intermediate_adapted_downsampled_data_Y);   
    av_free(intermediate_adapted_downsampled_data_U); 
    av_free(intermediate_adapted_downsampled_data_V); 
    
    av_free(adapted_downsampled_data_Y);  
    av_free(adapted_downsampled_data_U);
    av_free(adapted_downsampled_data_V); 
    
    av_free(delta_prediction_Y);  
    av_free(delta_prediction_U); 
    av_free(delta_prediction_V); 
}



/**
 * Writes MLHE delta frame 
 * 
 * @param *avctx Pointer to AVCodec context
 * @param *pkt Pointer to AVPacket 
 * @param total_blocks_width Number of blocks widthwise
 * @param total_blocks_height Number of blocks heightwise
 */
static int mlhe_advanced_write_delta_frame(AVCodecContext *avctx, AVPacket *pkt, 
                                           uint8_t total_blocks_width, uint8_t total_blocks_height) 
{
  
    uint8_t *buf;
    uint8_t lhe_mode, pr_interval;
    uint64_t n_bits_hops, n_bits_mesh, n_bytes, n_bytes_components, n_bytes_mesh, total_blocks;
    uint32_t xini_Y, xfin_downsampled_Y, yini_Y, yfin_downsampled_Y, xini_UV, xfin_downsampled_UV, yini_UV, yfin_downsampled_UV; 
    uint64_t pix;
        
    int i, ret;
            
    LheContext *s;
    LheProcessing *procY;
    LheProcessing *procUV;
    LheImage *lheY;
    LheImage *lheU;
    LheImage *lheV;
    
    LheHuffEntry he_mesh[LHE_MAX_HUFF_SIZE_MESH]; //Struct for mesh Huffman data
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE_SYMBOLS]; //Struct for luminance Huffman data
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE_SYMBOLS]; //Struct for chrominance Huffman data
        
    s = avctx->priv_data;
    procY = &s->procY;
    procUV = &s->procUV;
    lheY = &s->lheY;
    lheU = &s->lheU;
    lheV = &s->lheV;  
        
    total_blocks = total_blocks_height * total_blocks_width;
    
    //Generates HUffman
    n_bits_mesh = lhe_advanced_gen_huffman_mesh (he_mesh, 
                                                 procY->perceptual_relevance_x, procY->perceptual_relevance_y,                                          
                                                 total_blocks_width, total_blocks_height);
  
    n_bytes_mesh = (n_bits_mesh / 8) + 1;
    
    n_bits_hops = lhe_advanced_gen_huffman (he_Y, he_UV, procY, procUV, lheY, lheU, lheV,
                                            total_blocks_width, total_blocks_height);
     
    n_bytes_components = (n_bits_hops/8) + 1;           
    
    //File size
    n_bytes = sizeof(lhe_mode) 
              + total_blocks * (sizeof(*lheY->first_color_block) + sizeof(*lheU->first_color_block) + sizeof(*lheV->first_color_block)) 
              + LHE_HUFFMAN_TABLE_BYTES_MESH
              + LHE_HUFFMAN_TABLE_BYTES_SYMBOLS + //huffman table
              + n_bytes_mesh 
              + n_bytes_components; //components
                            
    //ff_alloc_packet2 reserves n_bytes of memory
    if ((ret = ff_alloc_packet2(avctx, pkt, n_bytes, 0)) < 0)
        return ret;

    buf = pkt->data; //Pointer to write buffer   
    init_put_bits(&s->pb, buf, n_bytes);
    
    //LHE Mode
    lhe_mode = DELTA_MLHE; 
    
    //Lhe mode byte
    put_bits(&s->pb, LHE_MODE_SIZE_BITS, lhe_mode);

    //Save first delta for each block
    for (i=0; i<total_blocks; i++) 
    {
        put_bits(&s->pb, FIRST_COLOR_SIZE_BITS, lheY->first_color_block[i]);
    }
    
    for (i=0; i<total_blocks; i++) 
    {
        put_bits(&s->pb, FIRST_COLOR_SIZE_BITS, lheU->first_color_block[i]);
    }
    
    for (i=0; i<total_blocks; i++) 
    {
        put_bits(&s->pb, FIRST_COLOR_SIZE_BITS, lheV->first_color_block[i]);
    }
         
    //Write Huffman tables 
    for (i=0; i<LHE_MAX_HUFF_SIZE_SYMBOLS; i++)
    {
        if (he_Y[i].len==255) he_Y[i].len=LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS_SYMBOLS, he_Y[i].len);
    }
    
    for (i=0; i<LHE_MAX_HUFF_SIZE_SYMBOLS; i++)
    {
        if (he_UV[i].len==255) he_UV[i].len=LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS_SYMBOLS, he_UV[i].len);
    } 
    
    for (i=0; i<LHE_MAX_HUFF_SIZE_MESH; i++)
    {
        if (he_mesh[i].len==255) he_mesh[i].len=LHE_HUFFMAN_NO_OCCURRENCES_MESH;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS_MESH, he_mesh[i].len);
    }
    
    //Write mesh. First PRX, then PRY because it eases the decoding task
    //Perceptual Relevance x intervals
    for (int block_y=0; block_y<total_blocks_height+1; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width+1; block_x++) 
        { 
            pr_interval = lhe_advanced_translate_pr_into_mesh(procY->perceptual_relevance_x[block_y][block_x]);
            put_bits(&s->pb, he_mesh[pr_interval].len, he_mesh[pr_interval].code);
        }
    }
    
     //Perceptual relevance y intervals
    for (int block_y=0; block_y<total_blocks_height+1; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width+1; block_x++) 
        { 
            pr_interval = lhe_advanced_translate_pr_into_mesh(procY->perceptual_relevance_y[block_y][block_x]);
            put_bits(&s->pb, he_mesh[pr_interval].len, he_mesh[pr_interval].code);

        }
    }
    
    //Write hops
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {
            xini_Y = procY->basic_block[block_y][block_x].x_ini;
            yini_Y = procY->basic_block[block_y][block_x].y_ini;
            
            xfin_downsampled_Y = procY->advanced_block[block_y][block_x].x_fin_downsampled;          
            yfin_downsampled_Y = procY->advanced_block[block_y][block_x].y_fin_downsampled;
               
            xini_UV = procUV->basic_block[block_y][block_x].x_ini;
            yini_UV = procUV->basic_block[block_y][block_x].y_ini;
            
            xfin_downsampled_UV = procUV->advanced_block[block_y][block_x].x_fin_downsampled;
            yfin_downsampled_UV = procUV->advanced_block[block_y][block_x].y_fin_downsampled;
         
            //LUMINANCE
            for (int y=yini_Y; y<yfin_downsampled_Y; y++) 
            {
                for (int x=xini_Y; x<xfin_downsampled_Y; x++) {
                    pix = y*procY->width + x;
                    put_bits(&s->pb, he_Y[lheY->hops[pix]].len , he_Y[lheY->hops[pix]].code);
                }
            }
            
            //CHROMINANCE U
            for (int y=yini_UV; y<yfin_downsampled_UV; y++) 
            {
                for (int x=xini_UV; x<xfin_downsampled_UV; x++) {
                    pix = y*procUV->width + x;
                    put_bits(&s->pb, he_UV[lheU->hops[pix]].len , he_UV[lheU->hops[pix]].code);
                }
            }
            
            //CHROMINANCE_V
            for (int y=yini_UV; y<yfin_downsampled_UV; y++) 
            {
                for (int x=xini_UV; x<xfin_downsampled_UV; x++) {
                    pix = y*procUV->width + x;
                    put_bits(&s->pb, he_UV[lheV->hops[pix]].len , he_UV[lheV->hops[pix]].code);
                }
            }
        }
    }

    flush_put_bits(&s->pb);
    
    return n_bytes;
}

//==================================================================
// LHE IMAGE FUNCTIONS
//==================================================================
/**
 * Free pointers
 * 
 * @param *s Pointers
 * @param total_blocks_width 
 * @param total_blocks_height 
 */
static void lhe_basic_free(LheContext *s, uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    av_free((&s->lheY)->component_prediction);
    av_free((&s->lheU)->component_prediction);
    av_free((&s->lheV)->component_prediction);
    av_free((&s->lheY)->hops);
    av_free((&s->lheU)->hops);
    av_free((&s->lheV)->hops);
    av_free((&s->lheY)->first_color_block);
    av_free((&s->lheU)->first_color_block);
    av_free((&s->lheV)->first_color_block);
        
    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procY)->basic_block[i]);
    }
    
    av_free((&s->procY)->basic_block);

        
    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procUV)->basic_block[i]);
    }
    
    av_free((&s->procUV)->basic_block);
}

/**
 * Free pointers
 * 
 * @param *s Pointers
 * @param total_blocks_width 
 * @param total_blocks_height 
 */
static void lhe_advanced_free(LheContext *s, uint32_t total_blocks_width, uint32_t total_blocks_height) 
{    
    lhe_basic_free(s, total_blocks_width, total_blocks_height);

    for (int i=0; i<total_blocks_height+1; i++) 
    {
        av_free((&s->procY)->perceptual_relevance_x[i]);
    }
    
    av_free((&s->procY)->perceptual_relevance_x);
    
    for (int i=0; i<total_blocks_height+1; i++) 
    {
        av_free((&s->procY)->perceptual_relevance_y[i]);
    }   
    
    av_free((&s->procY)->perceptual_relevance_y);

    //Advanced blocks    
    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procY)->advanced_block[i]); 
    }
    
    av_free((&s->procY)->advanced_block); 

        
    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procUV)->advanced_block[i]);
    }
    
    av_free((&s->procUV)->advanced_block); 
    
    av_free((&s->lheY)->downsampled_image);
    av_free((&s->lheU)->downsampled_image);
    av_free((&s->lheV)->downsampled_image);
}

/**
 * Image encode method
 * 
 * @param *avctx Codec context
 * @param *pkt AV ff_alloc_packet
 * @param *frame AV frame data
 * @param *got_packet indicates packet is ready
 */
static int lhe_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    uint8_t *component_original_data_Y, *component_original_data_U, *component_original_data_V;
    uint32_t total_blocks_width, total_blocks_height, total_blocks, pixels_block;
    uint32_t image_size_Y, image_size_UV;
    
    float compression_factor;
    
    struct timeval before , after;

    LheContext *s = avctx->priv_data;
    
    (&s->procY)->width = frame->width;
    (&s->procY)->height =  frame->height; 
    image_size_Y = (&s->procY)->width * (&s->procY)->height;

    (&s->procUV)->width = ((&s->procY)->width - 1)/s->chroma_factor_width + 1;
    (&s->procUV)->height = ((&s->procY)->height - 1)/s->chroma_factor_height + 1;
    image_size_UV = (&s->procUV)->width * (&s->procUV)->height;
    
    total_blocks_width = HORIZONTAL_BLOCKS;
    pixels_block = (&s->procY)->width / HORIZONTAL_BLOCKS;
    total_blocks_height = (&s->procY)->height / pixels_block;
    
    total_blocks = total_blocks_height * total_blocks_width;

    (&s->procY)->theoretical_block_width = (&s->procY)->width / total_blocks_width;
    (&s->procY)->theoretical_block_height = (&s->procY)->height / total_blocks_height;       

    (&s->procUV)->theoretical_block_width = (&s->procUV)->width / total_blocks_width;
    (&s->procUV)->theoretical_block_height = (&s->procUV)->height / total_blocks_height;
    
    
    //Pointers to different color components
    component_original_data_Y = frame->data[0];
    component_original_data_U = frame->data[1];
    component_original_data_V = frame->data[2];
      
    
    (&s->lheY)->component_prediction = av_calloc(image_size_Y , sizeof(uint8_t));  
    (&s->lheU)->component_prediction = av_calloc(image_size_UV , sizeof(uint8_t));
    (&s->lheV)->component_prediction = av_calloc(image_size_UV , sizeof(uint8_t)); 
    (&s->lheY)->hops = av_calloc(image_size_Y , sizeof(uint8_t));
    (&s->lheU)->hops = av_calloc(image_size_UV , sizeof(uint8_t));
    (&s->lheV)->hops = av_calloc(image_size_UV , sizeof(uint8_t));
    (&s->lheY)->first_color_block = av_calloc(total_blocks , sizeof(uint8_t));
    (&s->lheU)->first_color_block = av_calloc(total_blocks , sizeof(uint8_t));
    (&s->lheV)->first_color_block = av_calloc(total_blocks , sizeof(uint8_t));
    
    (&s->procY)->basic_block = av_calloc(total_blocks_height, sizeof(BasicLheBlock *));
    
    for (int i=0; i < total_blocks_height; i++)
    {
        (&s->procY)->basic_block[i] = av_calloc (total_blocks_width, sizeof(BasicLheBlock));  
    }
    
    (&s->procUV)->basic_block = av_calloc (total_blocks_height, sizeof(BasicLheBlock *));
    
    for (int i=0; i < total_blocks_height; i++)
    {
        (&s->procUV)->basic_block[i] = av_calloc (total_blocks_width, sizeof(BasicLheBlock));
    }
      
    gettimeofday(&before , NULL);
    
    if (s->basic_lhe) 
    {  
        //BASIC LHE        
        if(OPENMP_FLAGS == CONFIG_OPENMP) {
     
            lhe_basic_encode_frame_pararell (s, frame, component_original_data_Y, component_original_data_U, component_original_data_V, 
                                            total_blocks_width, total_blocks_height);      
        } else 
        {
            total_blocks_height = 1;
            total_blocks_width = 1;
            total_blocks = 1;
            
            lhe_basic_encode_frame_sequential (s, frame, component_original_data_Y, component_original_data_U, component_original_data_V);                
        }
        
        gettimeofday(&after , NULL);
        
        lhe_basic_write_file(avctx, pkt,image_size_Y, image_size_UV, total_blocks_width, total_blocks_height);  
        
        lhe_basic_free(s, total_blocks_width, total_blocks_height);                       
    } 
    else 
    {
        //ADVANCED LHE
        (&s->procY)->perceptual_relevance_x = av_calloc((total_blocks_height+1), sizeof(float*));  
    
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            (&s->procY)->perceptual_relevance_x[i] = av_calloc((total_blocks_width+1), sizeof(float));
        }
        
        (&s->procY)->perceptual_relevance_y = av_calloc((total_blocks_height+1), sizeof(float*)); 
        
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            (&s->procY)->perceptual_relevance_y[i] = av_calloc((total_blocks_width+1), sizeof(float));
        }   
    
        //Advanced blocks
        (&s->procY)->advanced_block = av_calloc (total_blocks_height, sizeof(AdvancedLheBlock *));
        
        for (int i=0; i < total_blocks_height; i++)
        {
            (&s->procY)->advanced_block[i] = av_calloc (total_blocks_width, sizeof(AdvancedLheBlock));
        }
        
        (&s->procUV)->advanced_block = av_calloc (total_blocks_height, sizeof(AdvancedLheBlock *));
        
        for (int i=0; i < total_blocks_height; i++)
        {
            (&s->procUV)->advanced_block[i] = av_calloc (total_blocks_width, sizeof(AdvancedLheBlock));
        }
        
        (&s->lheY)->downsampled_image = av_calloc (image_size_Y, sizeof(uint8_t));
        (&s->lheU)->downsampled_image = av_calloc (image_size_UV, sizeof(uint8_t));
        (&s->lheV)->downsampled_image = av_calloc (image_size_UV, sizeof(uint8_t));
        
        
        compression_factor =  lhe_advanced_encode (s, frame, 
                                                   component_original_data_Y, component_original_data_U, component_original_data_V,
                                                   total_blocks_width, total_blocks_height);
                                                   
        gettimeofday(&after , NULL);

        //av_log (NULL, AV_LOG_INFO, "Advanced LHE with ql = %d and cf = %f \n",s->ql, compression_factor); 
                   
        lhe_advanced_write_file(avctx, pkt, image_size_Y, image_size_UV, total_blocks_width, total_blocks_height);   
 
        lhe_advanced_free(s, total_blocks_width, total_blocks_height);
    }
    
    if(avctx->flags&AV_CODEC_FLAG_PSNR){
        lhe_compute_error_for_psnr (avctx, frame, component_original_data_Y, component_original_data_U, component_original_data_V); 
    }

    if (s->pr_metrics)
    {
        print_json_pr_metrics(&s->procY, total_blocks_width, total_blocks_height);  
    }

    //av_log (NULL, AV_LOG_INFO, " %.0lf; ",time_diff(before , after));
    //av_log(NULL, AV_LOG_INFO, "CodingTime %.0lf \n", time_diff(before , after));
    
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
 
    return 0;

}

//==================================================================
// ENCODE VIDEO
//==================================================================
/**
 * Video encode method
 * 
 * @param *avctx Codec context
 * @param *pkt AV packet
 * @param *frame AV frame data
 * @param *got_packet indicates packet is ready
 */
static int mlhe_encode_video(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    uint8_t *component_original_data_Y, *component_original_data_U, *component_original_data_V;
    uint32_t total_blocks_width, total_blocks_height, total_blocks, pixels_block;
    uint32_t image_size_Y, image_size_UV;
            
    LheContext *s = avctx->priv_data;
        
    (&s->procY)->width = frame->width;
    (&s->procY)->height =  frame->height; 
    image_size_Y = (&s->procY)->width * (&s->procY)->height;

    (&s->procUV)->width = ((&s->procY)->width - 1)/s->chroma_factor_width + 1;
    (&s->procUV)->height = ((&s->procY)->height - 1)/s->chroma_factor_height + 1;
    image_size_UV = (&s->procUV)->width * (&s->procUV)->height;
    
    total_blocks_width = HORIZONTAL_BLOCKS;
    pixels_block = (&s->procY)->width / HORIZONTAL_BLOCKS;
    total_blocks_height = (&s->procY)->height / pixels_block;
    
    total_blocks = total_blocks_height * total_blocks_width;

    (&s->procY)->theoretical_block_width = (&s->procY)->width / total_blocks_width;
    (&s->procY)->theoretical_block_height = (&s->procY)->height / total_blocks_height;       

    (&s->procUV)->theoretical_block_width = (&s->procUV)->width / total_blocks_width;
    (&s->procUV)->theoretical_block_height = (&s->procUV)->height / total_blocks_height;   
    
    //Pointers to different color components
    component_original_data_Y = frame->data[0];
    component_original_data_U = frame->data[1];
    component_original_data_V = frame->data[2];
    
    if (!(&s->lheY)->component_prediction)
        (&s->lheY)->component_prediction = av_calloc(image_size_Y , sizeof(uint8_t));  
    
    if (!(&s->lheU)->component_prediction)
        (&s->lheU)->component_prediction = av_calloc(image_size_UV , sizeof(uint8_t));
    
    if (!(&s->lheV)->component_prediction)
        (&s->lheV)->component_prediction = av_calloc(image_size_UV , sizeof(uint8_t));
    
    if (!(&s->lheY)->hops)
        (&s->lheY)->hops = av_calloc(image_size_Y , sizeof(uint8_t));
    
    if (!(&s->lheU)->hops)
        (&s->lheU)->hops = av_calloc(image_size_UV , sizeof(uint8_t));
    
    if (!(&s->lheV)->hops)
        (&s->lheV)->hops = av_calloc(image_size_UV , sizeof(uint8_t));
    
    if (!(&s->lheY)->first_color_block)
        (&s->lheY)->first_color_block = av_calloc(total_blocks , sizeof(uint8_t));
    
    if (!(&s->lheU)->first_color_block)
        (&s->lheU)->first_color_block = av_calloc(total_blocks , sizeof(uint8_t));
    
    if (!(&s->lheV)->first_color_block)
        (&s->lheV)->first_color_block = av_calloc(total_blocks , sizeof(uint8_t));
         
    /* GOP frames P, 1 frame I*/
    if ((&s->lheY)->last_downsampled_image && s->dif_frames_count<GOP) /*DELTA FRAMES */ 
    {
        s->dif_frames_count++;
        
        if (!(&s->lheY)->downsampled_error_image)
            (&s->lheY)->downsampled_error_image = av_calloc(image_size_Y, sizeof(uint8_t)); 
        
        if (!(&s->lheU)->downsampled_error_image)
            (&s->lheU)->downsampled_error_image = av_calloc(image_size_Y, sizeof(uint8_t)); 
        
        if (!(&s->lheV)->downsampled_error_image)
            (&s->lheV)->downsampled_error_image = av_calloc(image_size_Y, sizeof(uint8_t)); 
        
        mlhe_delta_frame_encode (s, frame,
                                 component_original_data_Y, component_original_data_U, component_original_data_V,
                                 total_blocks_width, total_blocks_height);
     
        mlhe_advanced_write_delta_frame(avctx, pkt, 
                                        total_blocks_width, total_blocks_height); 
        
        /*Copying information from last frame*/
        memcpy ((&s->lheY)->last_downsampled_image, (&s->lheY)->downsampled_error_image, image_size_Y);    
        memcpy ((&s->lheU)->last_downsampled_image, (&s->lheU)->downsampled_error_image, image_size_UV);
        memcpy ((&s->lheV)->last_downsampled_image, (&s->lheV)->downsampled_error_image, image_size_UV);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            memcpy((&s->procY)->last_advanced_block[i], (&s->procY)->advanced_block[i], sizeof(AdvancedLheBlock) * (total_blocks_width));
            memcpy((&s->procUV)->last_advanced_block[i], (&s->procUV)->advanced_block[i], sizeof(AdvancedLheBlock) * (total_blocks_width));
        } 
    }      
    else /*INTRA FRAMES */ 
    {
        /*Init dif frames count*/
        s->dif_frames_count = 0;
        
        (&s->procY)->basic_block = av_calloc(total_blocks_height, sizeof(BasicLheBlock *));
        
        for (int i=0; i < total_blocks_height; i++)
            (&s->procY)->basic_block[i] = av_calloc (total_blocks_width, sizeof(BasicLheBlock));  
        
        
        (&s->procUV)->basic_block = av_calloc (total_blocks_height, sizeof(BasicLheBlock *));
        
        for (int i=0; i < total_blocks_height; i++)     
            (&s->procUV)->basic_block[i] = av_calloc (total_blocks_width, sizeof(BasicLheBlock));
        

        (&s->procY)->perceptual_relevance_x = av_calloc((total_blocks_height+1), sizeof(float*));  
    
        for (int i=0; i<total_blocks_height+1; i++) 
            (&s->procY)->perceptual_relevance_x[i] = av_calloc((total_blocks_width+1), sizeof(float));
     
        (&s->procY)->perceptual_relevance_y = av_calloc((total_blocks_height+1), sizeof(float*)); 
        
        for (int i=0; i<total_blocks_height+1; i++) 
            (&s->procY)->perceptual_relevance_y[i] = av_calloc((total_blocks_width+1), sizeof(float));

        //Advanced blocks
        (&s->procY)->advanced_block = av_calloc (total_blocks_height, sizeof(AdvancedLheBlock *));
        
        for (int i=0; i < total_blocks_height; i++)
            (&s->procY)->advanced_block[i] = av_calloc (total_blocks_width, sizeof(AdvancedLheBlock));
 
        (&s->procUV)->advanced_block = av_calloc (total_blocks_height, sizeof(AdvancedLheBlock *));
        
        for (int i=0; i < total_blocks_height; i++)
            (&s->procUV)->advanced_block[i] = av_calloc (total_blocks_width, sizeof(AdvancedLheBlock));
        
        if (!(&s->lheY)->downsampled_image)
            (&s->lheY)->downsampled_image = av_calloc (image_size_Y, sizeof(uint8_t));
        
        if (!(&s->lheU)->downsampled_image)
            (&s->lheU)->downsampled_image = av_calloc (image_size_UV, sizeof(uint8_t));
        
        if (!(&s->lheV)->downsampled_image)
            (&s->lheV)->downsampled_image = av_calloc (image_size_UV, sizeof(uint8_t));
       
        lhe_advanced_encode (s, frame, component_original_data_Y, component_original_data_U, component_original_data_V,
                             total_blocks_width, total_blocks_height);     

        lhe_advanced_write_file(avctx, pkt, 
                                image_size_Y, image_size_UV, 
                                total_blocks_width, total_blocks_height);   
        
        /*Creating arrays for delta frames*/
        if (!(&s->lheY)->last_downsampled_image)
            (&s->lheY)->last_downsampled_image = av_calloc(image_size_Y, sizeof(uint8_t));  
        
        if(!(&s->lheU)->last_downsampled_image) 
            (&s->lheU)->last_downsampled_image = av_calloc(image_size_UV, sizeof(uint8_t)); 
            
        if(!(&s->lheV)->last_downsampled_image) 
            (&s->lheV)->last_downsampled_image = av_calloc(image_size_UV, sizeof(uint8_t));  
        
        if (!(&s->procY)->last_advanced_block) 
        {
            (&s->procY)->last_advanced_block = av_calloc(total_blocks_height, sizeof(AdvancedLheBlock *));
        
            for (int i=0; i < total_blocks_height; i++)
                (&s->procY)->last_advanced_block[i] = av_calloc (total_blocks_width, sizeof(AdvancedLheBlock));    
        }
    
        if (!(&s->procUV)->last_advanced_block) {
            (&s->procUV)->last_advanced_block = av_calloc(total_blocks_height, sizeof(AdvancedLheBlock *));
        
            for (int i=0; i < total_blocks_height; i++)
                (&s->procUV)->last_advanced_block[i] = av_calloc (total_blocks_width, sizeof(AdvancedLheBlock));
        }
        
        /*Copying information from last frame*/
        memcpy ((&s->lheY)->last_downsampled_image, (&s->lheY)->component_prediction, image_size_Y);    
        memcpy ((&s->lheU)->last_downsampled_image, (&s->lheU)->component_prediction, image_size_UV);
        memcpy ((&s->lheV)->last_downsampled_image, (&s->lheV)->component_prediction, image_size_UV);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            memcpy((&s->procY)->last_advanced_block[i], (&s->procY)->advanced_block[i], sizeof(AdvancedLheBlock) * (total_blocks_width));
            memcpy((&s->procUV)->last_advanced_block[i], (&s->procUV)->advanced_block[i], sizeof(AdvancedLheBlock) * (total_blocks_width));
        } 
    }
    

    if(avctx->flags&AV_CODEC_FLAG_PSNR){
        lhe_compute_error_for_psnr (avctx, frame, component_original_data_Y, component_original_data_U, component_original_data_V); 
    }
    
    if (s->pr_metrics)
    {
        print_csv_pr_metrics(&s->procY, total_blocks_width, total_blocks_height);  
    }
   

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;

}

static int lhe_encode_close(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    av_freep(&s->prec.prec_luminance);
    av_freep(&s->prec.best_hop);

    return 0;

}

static int mlhe_encode_close(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    av_free((&s->lheY)->component_prediction); 
    av_free((&s->lheU)->component_prediction); 
    av_free((&s->lheV)->component_prediction); 
    av_free((&s->lheY)->hops); 
    av_free((&s->lheU)->hops); 
    av_free((&s->lheV)->hops); 
    av_free((&s->lheY)->first_color_block);
    av_free((&s->lheU)->first_color_block); 
    av_free((&s->lheV)->first_color_block);
    av_free((&s->lheY)->last_downsampled_image);
    av_free((&s->lheU)->last_downsampled_image); 
    av_free((&s->lheV)->last_downsampled_image);
    av_free((&s->lheY)->downsampled_image);
    av_free((&s->lheU)->downsampled_image); 
    av_free((&s->lheV)->downsampled_image);
    av_free((&s->lheY)->downsampled_error_image);
    av_free((&s->lheU)->downsampled_error_image); 
    av_free((&s->lheV)->downsampled_error_image);
    
    return 0;
}

#define OFFSET(x) offsetof(LheContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "pr_metrics", "Print PR metrics", OFFSET(pr_metrics), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "basic_lhe", "Basic LHE", OFFSET(basic_lhe), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "ql", "Quality level from 0 to 99", OFFSET(ql), AV_OPT_TYPE_INT, { .i64 = 50 }, 0, 99, VE },
    { "subsampling_average", "Average subsampling. Otherwise, sps subsampling is done.", OFFSET(subsampling_average), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { NULL },
};


static const AVClass lhe_class = {
    .class_name = "LHE Basic encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};



AVCodec ff_lhe_encoder = {
    .name           = "lhe",
    .long_name      = NULL_IF_CONFIG_SMALL("LHE"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_LHE,
    .priv_data_size = sizeof(LheContext),
    .init           = lhe_encode_init,
    .encode2        = lhe_encode_frame,
    .close          = lhe_encode_close,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV420P, 
        AV_PIX_FMT_YUV422P, 
        AV_PIX_FMT_YUV444P, 
        AV_PIX_FMT_NONE
    },
    .priv_class     = &lhe_class,
};

AVCodec ff_mlhe_encoder = {
    .name           = "mlhe",
    .long_name      = NULL_IF_CONFIG_SMALL("MLHE"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MLHE,
    .priv_data_size = sizeof(LheContext),
    .init           = mlhe_encode_init,
    .encode2        = mlhe_encode_video,
    .close          = mlhe_encode_close,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV420P, 
        AV_PIX_FMT_YUV422P, 
        AV_PIX_FMT_YUV444P, 
        AV_PIX_FMT_NONE
    },
    .priv_class     = &lhe_class,
};
