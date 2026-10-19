// FrameSpeed v0.1, project RTL authored with AI assistance, 2026-10-04.
// RGB565 Fill/Copy; 32-bit AXI4 INCR; up to 32 beats; one read + one write outstanding.
// Stage 16 next-block prefetch overlapped with current-block writes.
// Stage 08 background-triggered texture reads + streaming Alpha: color key, uniform 8-bit Alpha, and combined key+Alpha.
// Foreground textures can be preloaded into two 4-KiB slots. Background is
// always read from the current destination, preserving ordered compositing.
// op=5: preload src (word aligned), width=word count (1..1024), height=1,
// color=slot (0/1). CPU must invalidate/reload after modifying texture memory.
// 0x54: bit0 enable, bit1 invalidate both on write; bits8/9 valid on read.
// 0x58/5c/60: cumulative hit bursts/miss bursts/foreground DDR bytes saved.
`timescale 1ns/1ps
`default_nettype none
module fs_blitter_legacy(
 input wire clk, input wire reset,
 input wire [15:0] paddr, input wire psel,penable,pwrite,
 input wire [31:0] pwdata, output reg [31:0] prdata,
 output wire pready, output wire pslverror, output wire busy_out,
 output wire [31:0] awaddr, output wire [7:0] awlen,
 output wire [2:0] awsize, output wire [1:0] awburst,
 output wire awvalid, input wire awready,
 output wire [31:0] wdata, output wire [3:0] wstrb,
 output wire wlast,wvalid, input wire wready,
 input wire [1:0] bresp, input wire bvalid, output wire bready,
 output wire [31:0] araddr, output wire [7:0] arlen,
 output wire [2:0] arsize, output wire [1:0] arburst,
 output wire arvalid, input wire arready,
 input wire [31:0] rdata, input wire [1:0] rresp,
 input wire rlast,rvalid, output wire rready
);
 localparam IDLE=0,CHECK=1,PLAN=2,RA=3,RD=4,WA=5,WD=6,BR=7,NEXT=8,SPAN=9,EXTENT=10,RANGE=11,BG_RA=12,BG_RD=13,BLEND=14,LOAD_PLAN=15,LOAD_NEXT=16,CACHE_RD=17,PF_DRAIN=18;
 localparam [31:0] DMA_LO=32'h00400000,DMA_HI=32'h00c00000;
 reg [4:0] state;
 // Read-ahead uses the other foreground/blend bank (32 words each), plus a
 // background staging buffer for cache misses. It never crosses a command.
 // Copy/key prefetch source; alpha prefetch CURRENT destination background.
 // Validated rows/pixels do not overlap. Even shared edge words use disjoint
 // halfword strobes, so the prefetched pixel is not modified by current writes.
 localparam PF_IDLE=0,PF_ADDR=1,PF_DATA=2,PF_BLEND=3,PF_DONE=4;
 reg [2:0] pf_state;
 reg pf_enabled,pf_use,pf_fault;
 reg bank,pf_bank,pf_cached_alpha,pf_slot,pf_single,pf_src_hi,pf_dst_hi;
 reg [9:0] pf_first;
 reg pf_q_valid;
 reg [31:0] pf_bg_q;
 reg [4:0] pf_q_index;
 reg [5:0] pf_retire,pf_received;
 reg [31:0] pf_mem[0:31],pf_addr;
 reg [5:0] pf_beats,pf_ri;
 reg [31:0] pf_hits,pf_overlap;
 reg next_exists;
 reg [31:0] next_src,next_dst,next_left;
 integer pn,pp;
 reg [5:0] next_beats;
 always @* begin
   pn=1;pp=1;next_beats=1;
   if(next_src[1]==next_dst[1])begin
     pn=next_left;
     if(pn>64-next_dst[1])pn=64-next_dst[1];
     pp=(4096-{20'd0,next_dst[11:0]})>>1;if(pn>pp)pn=pp;
     pp=(4096-{20'd0,next_src[11:0]})>>1;if(pn>pp)pn=pp;
     next_beats=(pn+next_dst[1]+1)>>1;
   end
 end
 wire pf_active=(pf_state==PF_ADDR || pf_state==PF_DATA || pf_state==PF_BLEND);
 reg [31:0] texture_mem[0:2047]; // synchronous read, no reset, inferred block RAM
 reg cache_enabled; reg [1:0] cache_valid;
 reg [31:0] cache_base0,cache_end0,cache_base1,cache_end1;
 reg [31:0] cache_hits,cache_misses,cache_saved;
 reg cache_slot;reg [9:0] cache_first;reg [10:0] load_index;
 reg [5:0] cache_issue,cache_retire;
 reg cache_q_valid;reg [31:0] cache_q;
 reg cached_alpha,bg_q_valid;
 reg [31:0] bg_q;reg [4:0] bg_index;
 reg [31:0] alpha_s,alpha;
 reg [31:0] blend_fifo[0:63];
 reg [5:0] blend_retire;
 reg done,error;
 reg [31:0] errcode,complete_seq,cycles,read_bytes,write_bytes;
 reg [15:0] key_s,key;
 reg [31:0] op_s,src_s,dst_s,ss_s,ds_s,width_s,height_s,color_s,seq_s;
 reg [31:0] op,src_line,dst_line,ss,ds,width,height,color,seq;
 reg [31:0] src_cur,dst_cur,left_pixels,row;
 wire [31:0] next_cache_addr={next_src[31:2],2'b00};
 wire [32:0] next_cache_limit={1'b0,next_cache_addr}+{25'd0,next_beats,2'b00};
 wire next_cached=cache_enabled &&
    ((cache_valid[0] && next_cache_addr>=cache_base0 && next_cache_limit<={1'b0,cache_end0}) ||
     (cache_valid[1] && next_cache_addr>=cache_base1 && next_cache_limit<={1'b0,cache_end1}));
 reg [5:0] beats,ri,wi;
 wire buffered_read=pf_use && ((op>=3 && (state==BG_RA || state==BG_RD)) ||
                              (op<3 && (state==RA || state==RD)));
 wire rv=buffered_read || (rvalid && pf_state!=PF_DATA);
 wire [31:0] rd=buffered_read?pf_mem[ri[4:0]]:rdata;
 wire [1:0] rr=buffered_read?2'b00:rresp;
 wire rl=buffered_read?(ri==beats-1):rlast;
 wire [9:0] cache_index=(pf_cached_alpha && pf_state==PF_DATA)?pf_first+pf_ri:cache_first+(cached_alpha?ri:cache_issue);
 reg [6:0] pixels;
 reg single_pixel,read_fault;
 reg [31:0] fifo [0:63]; // ping-pong foreground banks, current write + next read
 // Both ports address one shared RAM; control excludes simultaneous preload/read.
 always @(posedge clk)begin
   if(state==RD && op==5 && rvalid && ri<beats)
     texture_mem[{color[0],load_index[9:0]}]<=rdata;
   if((state==CACHE_RD && cache_issue<beats) || (cached_alpha && state==BG_RD && rv && ri<beats) ||
      (pf_cached_alpha && pf_state==PF_DATA && rvalid && pf_ri<pf_beats))
     cache_q<=texture_mem[{(pf_cached_alpha && pf_state==PF_DATA)?pf_slot:cache_slot,cache_index}];
 end
 wire busy=(state!=IDLE);
 assign busy_out=busy;
 wire wr=psel && penable && pwrite;
 wire start=wr && paddr==16'h0008 && pwdata[0];
 assign pready=1'b1;
 assign pslverror=(start || (wr && (paddr==16'h54 || paddr==16'h64))) && busy; // reject without replacing running command
 // CHECK establishes 1..4096 rows and <=65536 stride before these stages.
 // Pipeline bounded arithmetic instead of one large multiply/add/compare path.
 wire [11:0] last_row=height[11:0]-12'd1;
 reg [28:0] src_span,dst_span;
 reg [32:0] src_end,dst_end;
 integer n;
 integer page_beats;
 reg [5:0] plan_beats;
 reg [6:0] plan_pixels;
 reg plan_single;
 always @* begin
   n=1; page_beats=1; plan_beats=1; plan_pixels=1; plan_single=1;
   // When both halfword offsets match, include the first/last partial word
   // in one aligned burst. Different offsets retain the proven scalar path.
   if(op==0 || src_cur[1]==dst_cur[1]) begin
     n=left_pixels;
     // Limit sustained clear writes to the previously verified 16 beats.
     // Copies keep up to 32 beats; scanout receives read arbitration priority.
     if(n>(op==0 ? 32 : 64)-dst_cur[1])n=(op==0 ? 32 : 64)-dst_cur[1];
     page_beats=(4096-{20'd0,dst_cur[11:0]})>>1;
     if(n>page_beats)n=page_beats;
     if(op!=0)begin
       page_beats=(4096-{20'd0,src_cur[11:0]})>>1;
       if(n>page_beats)n=page_beats;
     end
     plan_pixels=n;plan_beats=(n+dst_cur[1]+1)>>1;plan_single=0;
   end
 end
 wire [31:0] cache_addr={src_cur[31:2],2'b00};
 wire [32:0] cache_limit={1'b0,cache_addr}+{25'd0,plan_beats,2'b00};
 wire cache_hit0=cache_enabled && cache_valid[0] && cache_addr>=cache_base0 && cache_limit<={1'b0,cache_end0};
 wire cache_hit1=cache_enabled && cache_valid[1] && cache_addr>=cache_base1 && cache_limit<={1'b0,cache_end1};
 assign awaddr={dst_cur[31:2],2'b00};
 assign araddr=pf_state==PF_ADDR ? pf_addr : (state==BG_RA || state==BG_RD) ? {dst_cur[31:2],2'b00} : {src_cur[31:2],2'b00};
 assign awlen={2'd0,beats}-8'd1;
 assign arlen=pf_state==PF_ADDR ? {2'd0,pf_beats}-8'd1 : {2'd0,beats}-8'd1;
 assign awsize=3'd2; assign arsize=3'd2;
 assign awburst=2'b01; assign arburst=2'b01;
 assign awvalid=(state==WA); assign arvalid=(pf_state==PF_ADDR) || (!buffered_read && (state==RA || state==BG_RA));
 assign rready=(pf_state==PF_DATA) || (!buffered_read && (state==RD || state==BG_RD)); assign wvalid=(state==WD);
 assign wlast=(wi==beats-1); assign bready=(state==BR);
 wire [15:0] selected_pixel=src_cur[1] ? fifo[{bank,5'd0}][31:16] : fifo[{bank,5'd0}][15:0];
 wire [31:0] copy_word=single_pixel ? (dst_cur[1] ? {selected_pixel,16'd0} : {16'd0,selected_pixel}) : fifo[{bank,wi[4:0]}];
 assign wdata=op==0 ? {color[15:0],color[15:0]} :
              op>=3 ? blend_fifo[{bank,wi[4:0]}] : copy_word;
 wire [7:0] end_halfwords={1'b0,pixels}+dst_cur[1];
 wire mask_low=!(wi==0 && dst_cur[1]);
 wire mask_high=!(wi==beats-1 && end_halfwords[0]);
 wire [3:0] pixel_strobes=single_pixel ? (dst_cur[1] ? 4'b1100 : 4'b0011) :
                         {{2{mask_high}},{2{mask_low}}};
 assign wstrb=(op==2 || op==4) ? (pixel_strobes & { {2{copy_word[31:16]!=key}}, {2{copy_word[15:0]!=key}} }) : pixel_strobes;
 wire blend_valid;
 wire [31:0] blend_word;
 wire [31:0] blend_fg=single_pixel ? (dst_cur[1] ? {selected_pixel,16'd0} : {16'd0,selected_pixel}) : fifo[{bank,ri[4:0]}];
 wire [15:0] cached_pixel=src_cur[1]?cache_q[31:16]:cache_q[15:0];
 wire [31:0] cached_fg=single_pixel?(dst_cur[1]?{cached_pixel,16'd0}:{16'd0,cached_pixel}):cache_q;
 wire alpha_input=cached_alpha ? (bg_q_valid && (state==BG_RD || state==BLEND)) : (state==BG_RD && rv && ri<beats);
 wire [15:0] pf_selected=pf_src_hi?cache_q[31:16]:cache_q[15:0];
 wire [31:0] pf_fg=pf_single?(pf_dst_hi?{pf_selected,16'd0}:{16'd0,pf_selected}):cache_q;
 wire pf_blending=pf_cached_alpha && (pf_state==PF_DATA || pf_state==PF_BLEND);
 fs_alpha565 u_alpha(.clk(clk),.reset(reset),.in_valid(pf_blending?pf_q_valid:alpha_input),
   .fg(pf_blending?pf_fg:(cached_alpha?cached_fg:blend_fg)),.bg(pf_blending?pf_bg_q:(cached_alpha?bg_q:rd)),.alpha(alpha[7:0]),
   .out_valid(blend_valid),.pixel(blend_word));
 always @* begin
   prdata=0;
   case(paddr)
     16'h00: prdata=32'h46530107;
     16'h04: prdata={29'd0,error,done,busy};
     16'h0c: prdata=op_s;
     16'h10: prdata=src_s; 16'h14: prdata=dst_s;
     16'h18: prdata=ss_s; 16'h1c: prdata=ds_s;
     16'h20: prdata=width_s; 16'h24: prdata=height_s;
     16'h30: prdata=alpha_s;
     16'h2c: prdata={16'd0,key_s};
     16'h54: prdata={22'd0,cache_valid,7'd0,cache_enabled};
     16'h58: prdata=cache_hits;16'h5c: prdata=cache_misses;16'h60: prdata=cache_saved;
     16'h64: prdata={31'd0,pf_enabled};
     16'h68: prdata=pf_hits;16'h6c: prdata=pf_overlap;
     16'h70: prdata=32'h50465231; // PFR1 extension signature
     16'h50: prdata=32'h3f; // fill/copy/key/alpha/key+alpha capabilities
     16'h28: prdata=color_s; 16'h34: prdata=seq_s;
     16'h38: prdata=complete_seq; 16'h3c: prdata=errcode;
     16'h40: prdata=cycles; 16'h44: prdata=read_bytes;
     16'h48: prdata=write_bytes;
     default: prdata=0;
   endcase
 end
 task finish;
   input [31:0] code;
   begin state<=IDLE; done<=1; error<=(code!=0); errcode<=code; complete_seq<=seq; end
 endtask
 always @(posedge clk) begin
   if(reset) begin
     bank<=0;pf_bank<=1;pf_cached_alpha<=0;pf_slot<=0;pf_first<=0;
     pf_single<=0;pf_src_hi<=0;pf_dst_hi<=0;pf_q_valid<=0;pf_bg_q<=0;
     pf_q_index<=0;pf_retire<=0;pf_received<=0;
     pf_state<=PF_IDLE;pf_enabled<=1;pf_use<=0;pf_fault<=0;
     pf_addr<=0;pf_beats<=1;pf_ri<=0;pf_hits<=0;pf_overlap<=0;
     next_exists<=0;next_src<=0;next_dst<=0;next_left<=0;
     cache_enabled<=0;cache_valid<=0;cache_base0<=0;cache_base1<=0;cache_end0<=0;cache_end1<=0;
     cache_hits<=0;cache_misses<=0;cache_saved<=0;cache_slot<=0;cache_first<=0;load_index<=0;
     cache_issue<=0;cache_retire<=0;cache_q_valid<=0;
     cached_alpha<=0;bg_q_valid<=0;bg_q<=0;bg_index<=0;
     state<=IDLE; done<=0; error<=0; errcode<=0;complete_seq<=0;
     cycles<=0;read_bytes<=0;write_bytes<=0;
     key_s<=0;key<=0;alpha_s<=255;alpha<=255;blend_retire<=0;
     op_s<=0;src_s<=0;dst_s<=0;ss_s<=0;ds_s<=0;width_s<=0;height_s<=0;color_s<=0;seq_s<=0;
     op<=0;src_line<=0;dst_line<=0;ss<=0;ds<=0;width<=0;height<=0;color<=0;seq<=0;
     src_cur<=0;dst_cur<=0;left_pixels<=0;row<=0;beats<=1;ri<=0;wi<=0;pixels<=0;single_pixel<=0;read_fault<=0;
     src_span<=0;dst_span<=0;src_end<=0;dst_end<=0;
   end else begin
     if(busy) cycles<=cycles+1;
     // AR and W channels may proceed independently; at most one AR outstanding.
     if(state==WA && pf_enabled && next_exists && op>=1 && op<=4 &&
        (op>=3 || !next_cached) && pf_state==PF_IDLE)begin
       pf_addr<=op>=3?{next_dst[31:2],2'b00}:{next_src[31:2],2'b00};
       pf_bank<=!bank;pf_cached_alpha<=op>=3 && next_cached;
       pf_single<=next_src[1]!=next_dst[1];pf_src_hi<=next_src[1];pf_dst_hi<=next_dst[1];
       pf_slot<=!(cache_valid[0] && next_cache_addr>=cache_base0 && next_cache_limit<={1'b0,cache_end0});
       pf_first<=(next_cache_addr-((cache_valid[0] && next_cache_addr>=cache_base0 && next_cache_limit<={1'b0,cache_end0})?cache_base0:cache_base1))>>2;
       pf_retire<=0;pf_received<=0;
       pf_beats<=next_beats;pf_ri<=0;pf_fault<=0;pf_state<=PF_ADDR;
     end
     if(pf_state==PF_ADDR && arready)pf_state<=PF_DATA;
     if(pf_state==PF_DATA && rvalid)begin
       if(pf_ri<pf_beats)begin
         if(op<3)fifo[{pf_bank,pf_ri[4:0]}]<=rdata;
         else if(!pf_cached_alpha)pf_mem[pf_ri[4:0]]<=rdata;
       end
       read_bytes<=read_bytes+4;
       if(state==WD && wvalid && wready)pf_overlap<=pf_overlap+1;
       if(rresp!=0 || rlast!=(pf_ri==pf_beats-1))pf_fault<=1;
       if(rlast)begin
         pf_received<=pf_ri<pf_beats?pf_ri+1:pf_beats;
         pf_state<=pf_cached_alpha?PF_BLEND:PF_DONE;
       end
       else if(pf_ri<63)pf_ri<=pf_ri+1;
     end
     pf_q_valid<=pf_cached_alpha && pf_state==PF_DATA && rvalid && pf_ri<pf_beats;
     if(pf_cached_alpha && pf_state==PF_DATA && rvalid && pf_ri<pf_beats)begin
       pf_bg_q<=rdata;pf_q_index<=pf_ri[4:0];
     end
     if(pf_blending && pf_q_valid)fifo[{pf_bank,pf_q_index}]<=cache_q;
     if(pf_blending && blend_valid)begin
       blend_fifo[{pf_bank,pf_retire[4:0]}]<=blend_word;pf_retire<=pf_retire+1;
     end
     if(pf_state==PF_BLEND && pf_retire==pf_received)pf_state<=PF_DONE;
     // One matched register delay for background and synchronous texture RAM.
     bg_q_valid<=cached_alpha && state==BG_RD && rv && ri<beats;
     if(cached_alpha && state==BG_RD && rv && ri<beats)begin bg_q<=rd;bg_index<=ri[4:0];end
     if(cached_alpha && bg_q_valid && (state==BG_RD || state==BLEND))fifo[{bank,bg_index}]<=cache_q;
     cache_q_valid<=(state==CACHE_RD && cache_issue<beats);
     if(state==CACHE_RD && cache_q_valid)begin
       fifo[{bank,cache_retire[4:0]}]<=cache_q;cache_retire<=cache_retire+1;
     end
     // Results may return while DDR is still supplying later background words.
     if((state==BG_RD || state==BLEND) && blend_valid)begin
       blend_fifo[{bank,blend_retire[4:0]}]<=blend_word;
       blend_retire<=blend_retire+1;
     end
     if(wr) begin
       case(paddr)
         16'h04: begin if(pwdata[1]) done<=0; if(pwdata[2]) begin error<=0;errcode<=0;end end
         16'h54: if(!busy)begin
           cache_enabled<=pwdata[0];if(pwdata[1])cache_valid<=0;
         end
         16'h64: if(!busy)pf_enabled<=pwdata[0];
         16'h0c: op_s<=pwdata;
         16'h10: src_s<=pwdata; 16'h14: dst_s<=pwdata;
         16'h18: ss_s<=pwdata; 16'h1c: ds_s<=pwdata;
         16'h20: width_s<=pwdata; 16'h24: height_s<=pwdata;
         16'h30: alpha_s<=pwdata;
         16'h2c: key_s<=pwdata[15:0];
         16'h28: color_s<=pwdata; 16'h34: seq_s<=pwdata;
         default: begin end
       endcase
     end
     case(state)
       IDLE: if(start) begin
         pf_state<=PF_IDLE;pf_use<=0;pf_fault<=0;
         key<=key_s;alpha<=alpha_s;
         op<=op_s; src_line<=src_s;dst_line<=dst_s;ss<=ss_s;ds<=ds_s;
         width<=width_s;height<=height_s;color<=color_s;seq<=seq_s;
         done<=0;error<=0;errcode<=0;cycles<=0;read_bytes<=0;write_bytes<=0;row<=0;
         state<=CHECK;
       end
       CHECK: begin
         if(op==5)begin
           if(color>1 || height!=1 || width==0 || width>1024 || src_line[1:0]!=0)finish(2);
           else if(src_line<DMA_LO || {1'b0,src_line}+({1'b0,width}<<2)>{1'b0,DMA_HI})finish(3);
           else begin
             cache_valid[color[0]]<=0;load_index<=0;src_cur<=src_line;
             left_pixels<=width;state<=LOAD_PLAN;
           end
         end
         else if(op>4) finish(1);
         else if(op>=3 && alpha>255) finish(2);
         else if(width==0 || height==0) finish(0);
         else if(width>4096 || height>4096 || ds>65536 || ds<(width<<1) || dst_line[0] || ds[0] ||
                  (op!=0 && (ss>65536 || ss<(width<<1) || src_line[0] || ss[0]))) finish(2);
         else state<=SPAN;
       end
       SPAN: begin
         src_span<=last_row*ss[16:0];dst_span<=last_row*ds[16:0];state<=EXTENT;
       end
       EXTENT: begin
         src_end<={1'b0,src_line}+{4'd0,src_span}+{19'd0,width[12:0],1'b0};
         dst_end<={1'b0,dst_line}+{4'd0,dst_span}+{19'd0,width[12:0],1'b0};
         state<=RANGE;
       end
       RANGE: begin
         if(dst_line<DMA_LO || dst_end>{1'b0,DMA_HI} ||
                  (op!=0 && (src_line<DMA_LO || src_end>{1'b0,DMA_HI}))) finish(3);
         else if(op!=0 && {1'b0,src_line}<dst_end && {1'b0,dst_line}<src_end) finish(4);
         else begin
           // Accelerator writes invalidate overlapping cached source regions.
           if({1'b0,dst_line}<{1'b0,cache_end0} && {1'b0,cache_base0}<dst_end)cache_valid[0]<=0;
           if({1'b0,dst_line}<{1'b0,cache_end1} && {1'b0,cache_base1}<dst_end)cache_valid[1]<=0;
           src_cur<=src_line;dst_cur<=dst_line;left_pixels<=width;state<=PLAN;
         end
       end
       PLAN: if(!pf_active)begin
        if(pf_state==PF_DONE && pf_fault)begin pf_state<=PF_IDLE;finish(5);end
        else begin
         pf_use<=pf_state==PF_DONE && op>=3 && !pf_cached_alpha;
         if(pf_state==PF_DONE)begin
           bank<=pf_bank;
           if(op<3 || pf_cached_alpha)pf_state<=PF_IDLE;
         end
         if(pf_state==PF_DONE)pf_hits<=pf_hits+1;
         next_exists<=!(left_pixels==plan_pixels && row==height-1);
         if(left_pixels==plan_pixels)begin
           next_src<=src_line+ss;next_dst<=dst_line+ds;next_left<=width;
         end else begin
           next_src<=src_cur+({25'd0,plan_pixels}<<1);
           next_dst<=dst_cur+({25'd0,plan_pixels}<<1);
           next_left<=left_pixels-plan_pixels;
         end
         cached_alpha<=0;
         beats<=plan_beats;pixels<=plan_pixels;single_pixel<=plan_single;
         ri<=0;wi<=0;read_fault<=0;
         if(op==0)state<=WA;
         else if(pf_state==PF_DONE && (op<3 || pf_cached_alpha))begin
           state<=WA;
           if(pf_cached_alpha)begin
             cache_hits<=cache_hits+1;cache_saved<=cache_saved+({26'd0,plan_beats}<<2);
           end else cache_misses<=cache_misses+1;
         end
         else if((cache_hit0 || cache_hit1) && !(pf_state==PF_DONE && op<3))begin
           cache_slot<=!cache_hit0;
           cache_first<=(cache_addr-(cache_hit0?cache_base0:cache_base1))>>2;
           cache_issue<=0;cache_retire<=0;cache_q_valid<=0;
           cached_alpha<=op>=3;state<=op>=3?BG_RA:CACHE_RD;
           cache_hits<=cache_hits+1;cache_saved<=cache_saved+({26'd0,plan_beats}<<2);
         end else begin cache_misses<=cache_misses+1;state<=RA;end
        end
       end
       LOAD_PLAN:begin
         if(left_pixels>32)beats<=32;else beats<=left_pixels[5:0];
         // Width is words here. A preload can cross a 4-KiB page only between bursts.
         if(((4096-{20'd0,src_cur[11:0]})>>2)<left_pixels && ((4096-{20'd0,src_cur[11:0]})>>2)<32)
           beats<=(4096-{20'd0,src_cur[11:0]})>>2;
         ri<=0;read_fault<=0;state<=RA;
       end
       LOAD_NEXT:begin
         if(left_pixels==beats)begin
           cache_valid[color[0]]<=1;
           if(color[0])begin cache_base1<=src_line;cache_end1<=src_line+(width<<2);end
           else begin cache_base0<=src_line;cache_end0<=src_line+(width<<2);end
           finish(0);
         end else begin left_pixels<=left_pixels-beats;src_cur<=src_cur+({26'd0,beats}<<2);state<=LOAD_PLAN;end
       end
       CACHE_RD:begin
         if(cache_issue<beats)cache_issue<=cache_issue+1;
         if(cache_retire==beats)begin ri<=0;state<=op>=3?BG_RA:WA;end
       end
       RA: if(buffered_read || arready) state<=RD;
       RD: if(rv) begin
         if(ri<beats)begin
           if(op==5)load_index<=load_index+1;
           else fifo[{bank,ri[4:0]}]<=rd;
         end
         if(!buffered_read)read_bytes<=read_bytes+4;
         if(rr!=0 || (rl!=(ri==beats-1))) read_fault<=1;
         if(rl) begin
           if(buffered_read)begin pf_use<=0;pf_state<=PF_IDLE;end
           if(read_fault || rr!=0 || ri!=beats-1) finish(5);
           else if(op==5)state<=LOAD_NEXT;
           else if(op>=3)begin ri<=0;state<=BG_RA;end
           else state<=WA;
         end else if(ri<63) ri<=ri+1;
       end
       BG_RA: if(buffered_read || arready)begin blend_retire<=0;state<=BG_RD;end
       BG_RD: if(rv) begin
         if(!buffered_read)read_bytes<=read_bytes+4;
         if(rr!=0 || (rl!=(ri==beats-1)))read_fault<=1;
         if(rl)begin
           if(buffered_read)begin pf_use<=0;pf_state<=PF_IDLE;end
           if(read_fault || rr!=0 || ri!=beats-1)finish(5);
           else state<=BLEND;
         end else if(ri<63)ri<=ri+1;
       end
       BLEND: if(blend_retire==beats)begin wi<=0;state<=WA;end
       WA: if(awready) state<=WD;
       WD: if(wready) begin
         write_bytes<=write_bytes+{30'd0,wstrb[0],1'b0}+{30'd0,wstrb[2],1'b0};
         if(wlast) state<=BR; else wi<=wi+1;
       end
       BR: if(bvalid) begin
         if(bresp!=0)begin
           if(pf_active)state<=PF_DRAIN;
           else begin pf_state<=PF_IDLE;pf_use<=0;finish(6);end
         end else state<=NEXT;
       end
       PF_DRAIN: if(!pf_active)begin pf_state<=PF_IDLE;pf_use<=0;finish(6);end
       NEXT: begin
         if(left_pixels==pixels) begin
           if(row==height-1) finish(0);
           else begin
             row<=row+1;src_line<=src_line+ss;dst_line<=dst_line+ds;
             src_cur<=src_line+ss;dst_cur<=dst_line+ds;left_pixels<=width;state<=PLAN;
           end
         end else begin
           left_pixels<=left_pixels-pixels;src_cur<=src_cur+(pixels<<1);dst_cur<=dst_cur+(pixels<<1);state<=PLAN;
         end
       end
       default: finish(7);
     endcase
   end
 end
endmodule
`default_nettype wire

