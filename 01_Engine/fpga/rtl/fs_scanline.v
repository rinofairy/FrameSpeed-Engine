// Experimental batch/scanline compositor. Project RTL, AI-assisted, 2026-10-13.
// Does not race scanout: writes a complete DDR BACK buffer before presentation.
// One pixel/clock composition; ordered descriptors; drain between sprites.
// Texture storage: 4096 x 32 bits. Two ping-pong RGB565 rows. No RAM reset.
`timescale 1ns/1ps
`default_nettype none
module fs_scanline(
 input wire clk,reset,
 input wire [15:0] paddr,input wire psel,penable,pwrite,
 input wire [31:0] pwdata,output reg [31:0] prdata,
 output wire pready,pslverror,output wire busy,
 output wire [31:0] awaddr,output wire [7:0] awlen,
 output wire [2:0] awsize,output wire [1:0] awburst,
 output wire awvalid,input wire awready,
 output wire [31:0] wdata,output wire [3:0] wstrb,
 output wire wlast,wvalid,input wire wready,
 input wire [1:0] bresp,input wire bvalid,output wire bready,
 output wire [31:0] araddr,output wire [7:0] arlen,
 output wire [2:0] arsize,output wire [1:0] arburst,
 output wire arvalid,input wire arready,
 input wire [31:0] rdata,input wire [1:0] rresp,
 input wire rlast,rvalid,output wire rready
);
 localparam IDLE=0,VALIDATE=1,VFETCH=2,VMUL=3,VCHECK=4,
 CLEAR=5,FETCH=6,SELECT=7,ADDRESS=8,DRAW=9,DRAIN=10,
 WPLAN=11,WA=12,WPRIME=13,WD=14,BR=15,
 LPLAN=16,LA=17,LD=18,FINISH=19,FRAME_SPAN=20,FRAME_CHECK=21,BGPRIME=22,BGCLEAR=23,BWAIT=24,BCPRIME=25,BGCOPY=26,
 CPLAN=27,CA=28,CD=29;
 reg [4:0] state;
 // Composition and AXI writeback are independent. Each row buffer has one
 // owner until the LAST BRESP for that row has been accepted. 0xf0 bit0 can
 // disable overlap at runtime for a same-bitstream serial comparison.
 localparam WX_IDLE=0,WX_PLAN=1,WX_A=2,WX_PRIME=3,WX_DATA=4,WX_B=5;
 reg [2:0] wx_state;
 reg dual_enable_s,dual_enabled,compose_bank,writer_bank,writer_next;
 reg [1:0] row_ready;
 reg [31:0] bank_addr0,bank_addr1,wx_addr;
 reg [8:0] wx_word;
 reg [5:0] wx_beats,wx_beat;
 reg writer_fault;
 reg done,error;reg [31:0] errcode,seq_s,complete_seq,cycles,read_bytes,write_bytes;
 reg [31:0] dst_s,stride_s,geometry_s,count_s,bg_s,tex_src_s,tex_words_s,burst_s;
 // Command 3 pulls count*16 bytes from DDR before the original compositor.
 // Command 1 retains the exact APB upload path for A/B comparison.
 reg [31:0] command_src_s,command_bytes,command_bursts;
 reg [31:0] dst,row_addr,load_addr;
 reg [31:0] bg_src_s,bg_row_addr;reg bg_ddr;
 localparam PF_IDLE=0,PF_PLAN=1,PF_A=2,PF_R=3,PF_DONE=4;
 reg [2:0] pf_state;reg [31:0] pf_addr;
 reg [8:0] pf_index;reg [5:0] pf_beats,pf_beat;reg pf_fault;
 reg [31:0] background_line[0:511];reg [31:0] bg_q;
 integer pf_planned,pf_remaining,pf_page;
 wire [11:0] pf_offset=pf_addr[11:0]+{pf_index,2'b00};
 reg [15:0] stride;
 reg [9:0] width,height,row;
 reg [9:0] count,di;
 reg [15:0] background;
 // 0x9c bit31: resident 64-pixel-wide background, nearest 10x8 expansion.
 // [24:16]: output Y origin; [15:0]: even texture pixel offset. [30:25] reserved.
 reg bg_enabled;reg [8:0] bg_origin;reg [12:0] bg_base;
 reg [5:0] bg_column;reg [2:0] bg_repeat;reg [31:0] bg_span;
 wire [9:0] bg_global_row={1'b0,bg_origin}+row;
 wire [5:0] bg_read_column=state==BGPRIME?6'd0:
                      bg_repeat==4?bg_column+1'b1:bg_column;
 wire [15:0] bg_read_pixel={3'd0,bg_base}+{3'd0,bg_global_row[9:3],6'd0}+bg_read_column;
 reg [5:0] burst_limit,beats,beat;
 reg [12:0] load_left,load_index,valid_words;
 reg load_fault;
 reg [11:0] upload; // sequential 4-word descriptors, 512 maximum
 reg [31:0] d0mem[0:511],d1mem[0:511],d2mem[0:511],d3mem[0:511];
 reg [31:0] d0,d1,d2,d3,span;
 // Shared immutable row intervals, indexed by a descriptor's first source row.
 // Entry: [12:0] absolute first texture pixel, [26:13] exclusive end (<=8192).
 // Descriptor remains four words: d0[15]=enable, d0[31:30]=row base[7:6],
 // d3[31:26]=row base[5:0]. Existing source/stride fields retain their limits.
 reg [31:0] span_memory[0:255];reg [31:0] span_q;
 reg [8:0] span_count;reg [15:0] span_key;
 reg [31:0] skipped_pixels;
 wire [7:0] span_base={d0[31:30],d3[31:26]};
 wire use_span=d0[15] && d3[24] && d3[15:0]==span_key;
 reg [24:0] frame_span;
 wire [15:0] sx=d1[15:0],sy=d1[31:16],sw=d2[15:0],sh=d2[31:16];
 reg [31:0] texture[0:4095];
 reg [15:0] row0_even[0:511],row0_odd[0:511];
 reg [15:0] row1_even[0:511],row1_odd[0:511];
 reg [8:0] clear_index,write_word;
 reg [9:0] issue_x,retired;
 reg [9:0] draw_offset,draw_width;
 reg [12:0] src_pixel;
 reg [31:0] tex_q;
 wire [15:0] bg_cached_pixel=bg_column[0]?tex_q[31:16]:tex_q[15:0];
 reg [15:0] row0_even_q,row0_odd_q,row1_even_q,row1_odd_q;
 wire [15:0] even_q=compose_bank?row1_even_q:row0_even_q;
 wire [15:0] odd_q=compose_bank?row1_odd_q:row0_odd_q;
 reg q_valid,q_src_hi,q_dst_hi;
 reg [9:0] q_x;
 wire [15:0] foreground=q_src_hi?tex_q[31:16]:tex_q[15:0];
 wire [15:0] backdrop=q_dst_hi?odd_q:even_q;
 wire alpha_valid;wire [31:0] alpha_pixel;
 fs_alpha565 mix(.clk(clk),.reset(reset),.in_valid(q_valid),
   .fg({16'd0,foreground}),.bg({16'd0,backdrop}),
   .alpha(d3[25]?d3[23:16]:8'd255),.out_valid(alpha_valid),.pixel(alpha_pixel));
 reg [9:0] x0,x1,x2;
 reg keep0,keep1,keep2;
 wire drawing_read=state==DRAW && issue_x<draw_width;
 wire flushing_read=wx_state==WX_PRIME || (wx_state==WX_DATA && wready && !wlast);
 wire [9:0] drawing_x=sx[9:0]+draw_offset+issue_x;
 wire [8:0] writer_read_index=wx_state==WX_PRIME?wx_word:wx_word+wx_beat+1'b1;
 wire read0=(drawing_read && !compose_bank) || (flushing_read && !writer_bank);
 wire read1=(drawing_read && compose_bank) || (flushing_read && writer_bank);
 wire [8:0] read0_index=(drawing_read && !compose_bank)?drawing_x[9:1]:writer_read_index;
 wire [8:0] read1_index=(drawing_read && compose_bank)?drawing_x[9:1]:writer_read_index;
 wire line_even_write=state==CLEAR || state==BGCLEAR || state==BGCOPY || (alpha_valid && keep2 && !x2[0]);
 wire line_odd_write=state==CLEAR || state==BGCLEAR || state==BGCOPY || (alpha_valid && keep2 && x2[0]);
 wire [8:0] line_write_index=(state==CLEAR || state==BGCLEAR || state==BGCOPY)?clear_index:x2[9:1];
 wire [15:0] line_write_data=state==CLEAR?background:state==BGCLEAR?bg_cached_pixel:alpha_pixel[15:0];
 wire [8:0] frame_last=height[8:0]-9'd1;
 wire [8:0] texture_last=sh[8:0]-9'd1;
 wire [8:0] texture_row=row[8:0]-sy[8:0];
 wire [8:0] span_index={1'b0,span_base}+texture_row;
 wire [24:0] frame_product=frame_last*stride;
 wire [22:0] texture_product=texture_last*d0[29:16];
 wire [22:0] row_product=texture_row*d0[29:16];
 wire [23:0] source_row={11'd0,d0[12:0]}+{1'b0,row_product};
 wire [23:0] crop_end=source_row+{8'd0,sw};
 wire [23:0] interval_first={11'd0,span_q[12:0]};
 wire [23:0] interval_end={10'd0,span_q[26:13]};
 wire [23:0] draw_first=use_span && interval_first>source_row?interval_first:source_row;
 wire [23:0] draw_end=use_span && interval_end<crop_end?interval_end:crop_end;
 wire [23:0] active_width=draw_end-draw_first;
 reg [31:0] pixel_count,descriptor_visits;
 wire wr=psel && penable && pwrite;
 wire config_error=(paddr==16'hb4 && upload>=2048) ||
   (paddr==16'he4 && (span_count>=256 || pwdata[31:27]!=0 ||
    {1'b0,pwdata[12:0]}>pwdata[26:13] || pwdata[26:13]>({1'b0,valid_words}<<1))) ||
   (paddr==16'hec && pwdata[31:16]!=0);
 assign busy=state!=IDLE;
 assign pready=1'b1;
 assign pslverror=wr && (busy || config_error);
 wire commit=wr && !busy && !config_error;
 // Runtime burst length stays <=32 and never crosses a 4 KiB boundary.
 integer planned,remaining,page;
 integer wx_planned,wx_remaining,wx_page;
 wire [11:0] wx_offset=wx_addr[11:0]+{wx_word,2'b00};
 wire publish_row=state==WPLAN && !row_ready[compose_bank] && !writer_fault;
 wire writer_row_done=wx_state==WX_B && bvalid && (bresp!=0 || wx_word+wx_beats==(width>>1));
 always @* begin
   pf_remaining=(width>>1)-pf_index;pf_page=(4096-{1'b0,pf_offset})>>2;
   pf_planned=pf_remaining;if(pf_planned>burst_limit)pf_planned=burst_limit;
   if(pf_planned>pf_page)pf_planned=pf_page;
 end
 always @* begin
   remaining=load_left;
   page=(4096-{1'b0,load_addr[11:0]})>>2;
   planned=remaining;if(planned>burst_limit)planned=burst_limit;
   if(planned>page)planned=page;
 end
 always @* begin
   wx_remaining=(width>>1)-wx_word;wx_page=(4096-{1'b0,wx_offset})>>2;
   wx_planned=wx_remaining;if(wx_planned>burst_limit)wx_planned=burst_limit;
   if(wx_planned>wx_page)wx_planned=wx_page;
 end
 wire loading=state==LA || state==LD || state==CA || state==CD;
 assign araddr=loading?load_addr:pf_addr+{21'd0,pf_index,2'b00};
 assign arlen=(loading?{2'd0,beats}:{2'd0,pf_beats})-1'b1;
 assign arsize=2;assign arburst=1;assign arvalid=state==LA || state==CA || pf_state==PF_A;
 assign rready=state==LD || state==CD || pf_state==PF_R;
 assign awaddr=wx_addr+{21'd0,wx_word,2'b00};assign awlen={2'd0,wx_beats}-1'b1;
 assign awsize=2;assign awburst=1;assign awvalid=wx_state==WX_A;
 assign wdata=writer_bank?{row1_odd_q,row1_even_q}:{row0_odd_q,row0_even_q};
 assign wstrb=4'hf;assign wvalid=wx_state==WX_DATA;
 assign wlast=wx_beat==wx_beats-1'b1;assign bready=wx_state==WX_B;
 // RAM ports. Synchronous reads, no combinational frame-sized RAM multiplexor.
 always @(posedge clk)begin
   if(commit && paddr==16'he4)span_memory[span_count[7:0]]<=pwdata;
   if(state==SELECT && use_span)span_q<=span_memory[span_index[7:0]];
   // A single canonical write port per descriptor RAM; no reset of RAMs.
   if((commit && paddr==16'hb4) || (state==CD && rvalid))case(state==CD?load_index[1:0]:upload[1:0])
     0:d0mem[state==CD?load_index[10:2]:upload[10:2]]<=state==CD?rdata:pwdata;
     1:d1mem[state==CD?load_index[10:2]:upload[10:2]]<=state==CD?rdata:pwdata;
     2:d2mem[state==CD?load_index[10:2]:upload[10:2]]<=state==CD?rdata:pwdata;
     3:d3mem[state==CD?load_index[10:2]:upload[10:2]]<=state==CD?rdata:pwdata;
   endcase
   if(state==VFETCH || state==FETCH)begin
     d0<=d0mem[di[8:0]];d1<=d1mem[di[8:0]];
     d2<=d2mem[di[8:0]];d3<=d3mem[di[8:0]];
   end
   if(state==LD && rvalid)texture[load_index[11:0]]<=rdata;
   if(drawing_read || state==BGPRIME || state==BGCLEAR)begin
     tex_q<=texture[drawing_read?src_pixel[12:1]:bg_read_pixel[12:1]];
   end
 end
 // One row of prefetch storage; next-row DMA runs while the current row
 // draws/writes. Refill starts only after the old row has been copied out.
 always @(posedge clk)begin
   if(pf_state==PF_R && rvalid)background_line[pf_index+pf_beat]<=rdata;
   if(state==BCPRIME || (state==BGCOPY && clear_index+1<(width>>1)))
     bg_q<=background_line[state==BCPRIME?9'd0:clear_index+1'b1];
 end
 always @(posedge clk)begin
   if(reset)begin pf_state<=PF_IDLE;pf_addr<=0;pf_index<=0;pf_beats<=1;pf_beat<=0;pf_fault<=0;end
   else if(commit && paddr==16'h88)begin pf_state<=PF_IDLE;pf_fault<=0;end
   else case(pf_state)
     PF_IDLE:if(state==BWAIT || (state==BGCOPY && clear_index+1==(width>>1) && row+1<height))begin
       pf_addr<=state==BWAIT?bg_row_addr:bg_row_addr+stride;
       pf_index<=0;pf_fault<=0;pf_state<=PF_PLAN;
     end
     PF_PLAN:begin pf_beats<=pf_planned;pf_beat<=0;pf_state<=PF_A;end
     PF_A:if(arready)pf_state<=PF_R;
     PF_R:if(rvalid)begin
       pf_beat<=pf_beat+1'b1;
       if(rresp!=0 || rlast!=(pf_beat==pf_beats-1))pf_fault<=1;
       if(rlast)begin
         if(pf_fault || rresp!=0 || pf_beat!=pf_beats-1 || pf_index+pf_beats==(width>>1))pf_state<=PF_DONE;
         else begin pf_index<=pf_index+pf_beats;pf_state<=PF_PLAN;end
       end
     end
     PF_DONE:if(state==BWAIT)pf_state<=PF_IDLE;
   endcase
 end
 // Canonical single-write/synchronous-read RAM ports. Multiplex addresses BEFORE
 // the memory, so clear and alpha writes do not infer distinct physical ports.
 always @(posedge clk)begin
   if(line_even_write && !compose_bank)row0_even[line_write_index]<=state==BGCOPY?bg_q[15:0]:line_write_data;
   if(read0)row0_even_q<=row0_even[read0_index];
 end
 always @(posedge clk)begin
   if(line_odd_write && !compose_bank)row0_odd[line_write_index]<=state==BGCOPY?bg_q[31:16]:line_write_data;
   if(read0)row0_odd_q<=row0_odd[read0_index];
 end
 always @(posedge clk)begin
   if(line_even_write && compose_bank)row1_even[line_write_index]<=state==BGCOPY?bg_q[15:0]:line_write_data;
   if(read1)row1_even_q<=row1_even[read1_index];
 end
 always @(posedge clk)begin
   if(line_odd_write && compose_bank)row1_odd[line_write_index]<=state==BGCOPY?bg_q[31:16]:line_write_data;
   if(read1)row1_odd_q<=row1_odd[read1_index];
 end
 // Pending flags are set only after Alpha has drained, and cleared only
 // after a row has completely written. RAMs themselves are never reset.
 always @(posedge clk)begin
   if(reset || (commit && paddr==16'h88))begin row_ready<=0;bank_addr0<=0;bank_addr1<=0;end
   else begin
     if(publish_row)begin
       row_ready[compose_bank]<=1;
       if(compose_bank)bank_addr1<=row_addr;else bank_addr0<=row_addr;
     end
     if(writer_row_done)row_ready[writer_bank]<=0;
   end
 end
 always @(posedge clk)begin
   if(reset || (commit && paddr==16'h88))begin
     wx_state<=WX_IDLE;writer_bank<=0;writer_next<=0;wx_addr<=0;
     wx_word<=0;wx_beats<=1;wx_beat<=0;writer_fault<=0;
   end else case(wx_state)
     WX_IDLE:if(busy && state!=FINISH && !error && !writer_fault && row_ready[writer_next])begin
       writer_bank<=writer_next;wx_addr<=writer_next?bank_addr1:bank_addr0;
       wx_word<=0;wx_state<=WX_PLAN;
     end
     WX_PLAN:if(error || state==FINISH)wx_state<=WX_IDLE;
       else begin wx_beats<=wx_planned;wx_beat<=0;wx_state<=WX_A;end
     // Once AWVALID is asserted it remains stable until accepted, even if
     // a simultaneous background read fails. Drain that write before done.
     WX_A:if(awready)wx_state<=WX_PRIME;
     WX_PRIME:wx_state<=WX_DATA;
     WX_DATA:if(wready)begin wx_beat<=wx_beat+1'b1;if(wlast)wx_state<=WX_B;end
     WX_B:if(bvalid)begin
       if(bresp!=0)begin writer_fault<=1;wx_state<=WX_IDLE;end
       else if(error || state==FINISH)wx_state<=WX_IDLE;
       else if(wx_word+wx_beats==(width>>1))begin writer_next<=!writer_bank;wx_state<=WX_IDLE;end
       else begin wx_word<=wx_word+wx_beats;wx_state<=WX_PLAN;end
     end
   endcase
 end
 always @* begin
   prdata=0;
   case(paddr)
    'h80:prdata=32'h46534c06;
    'h84:prdata={29'd0,error,done,busy};'h8c:prdata=dst_s;'h90:prdata=stride_s;
    'h94:prdata=geometry_s;'h98:prdata=count_s;'h9c:prdata=bg_s;
    'ha0:prdata=tex_src_s;'ha4:prdata=tex_words_s;'ha8:prdata=command_src_s;'hac:prdata=seq_s;
    'hb0:prdata={20'd0,upload};'hb8:prdata=burst_s;'hbc:prdata=errcode;
    'hc0:prdata=cycles;'hc4:prdata=read_bytes;'hc8:prdata=write_bytes;
    'hcc:prdata=complete_seq;'hd0:prdata={19'd0,valid_words};
    'hd4:prdata=pixel_count;'hd8:prdata=descriptor_visits;'hdc:prdata=bg_src_s;
    'he8:prdata={23'd0,span_count};'hec:prdata={16'd0,span_key};
    'hf0:prdata=32'h00040100|{31'd0,dual_enable_s};'hf4:prdata=skipped_pixels;
    'hf8:prdata=command_bytes;'hfc:prdata=command_bursts;
   endcase
 end
 always @(posedge clk)begin
   if(reset)begin
     state<=IDLE;done<=0;error<=0;errcode<=0;complete_seq<=0;seq_s<=0;
     dst_s<=0;stride_s<=0;geometry_s<=0;count_s<=0;bg_s<=0;
     tex_src_s<=0;tex_words_s<=0;burst_s<=16;upload<=0;valid_words<=0;
     command_src_s<=0;command_bytes<=0;command_bursts<=0;
     cycles<=0;read_bytes<=0;write_bytes<=0;pixel_count<=0;descriptor_visits<=0;
     span_count<=0;span_key<=0;skipped_pixels<=0;draw_offset<=0;draw_width<=0;
     q_valid<=0;keep0<=0;keep1<=0;keep2<=0;load_fault<=0;
     dst<=0;row_addr<=0;load_addr<=0;stride<=0;width<=0;height<=0;row<=0;
     count<=0;di<=0;background<=0;burst_limit<=16;beats<=1;beat<=0;
     bg_src_s<=0;bg_row_addr<=0;bg_ddr<=0;
     dual_enable_s<=1;dual_enabled<=1;compose_bank<=0;
     bg_enabled<=0;bg_origin<=0;bg_base<=0;bg_column<=0;bg_repeat<=0;bg_span<=0;
     load_left<=0;load_index<=0;clear_index<=0;write_word<=0;issue_x<=0;retired<=0;src_pixel<=0;
     q_src_hi<=0;q_dst_hi<=0;q_x<=0;x0<=0;x1<=0;x2<=0;span<=0;frame_span<=0;
   end else begin
     q_valid<=state==DRAW && issue_x<draw_width;
     if(state==DRAW && issue_x<draw_width)begin
       q_src_hi<=src_pixel[0];q_dst_hi<=drawing_x[0];q_x<=drawing_x;
     end
     x0<=q_x;x1<=x0;x2<=x1;
     keep0<=q_valid && (!d3[24] || foreground!=d3[15:0]);keep1<=keep0;keep2<=keep1;
     if(busy)cycles<=cycles+1'b1;
     if(wvalid && wready)write_bytes<=write_bytes+4;
     if(pf_state==PF_R && rvalid)read_bytes<=read_bytes+4;
     if(alpha_valid)begin retired<=retired+1'b1;pixel_count<=pixel_count+1'b1;end
     if(commit)case(paddr)
       'h8c:dst_s<=pwdata;'h90:stride_s<=pwdata;'h94:geometry_s<=pwdata;
       'h98:count_s<=pwdata;'h9c:bg_s<=pwdata;'ha0:tex_src_s<=pwdata;
       'ha4:tex_words_s<=pwdata;'hac:seq_s<=pwdata;
       'ha8:command_src_s<=pwdata;
       'hf0:dual_enable_s<=pwdata[0];
       'hdc:bg_src_s<=pwdata;
       'he0:span_count<=0;'he4:span_count<=span_count+1'b1;'hec:span_key<=pwdata[15:0];
       'hb0:upload<=0; // only rewind, disallows holes/uninitialised descriptors
       'hb4:upload<=upload+1'b1;'hb8:burst_s<=pwdata;
       'h88:begin
         done<=0;error<=0;errcode<=0;cycles<=0;read_bytes<=0;write_bytes<=0;
         pixel_count<=0;descriptor_visits<=0;
         skipped_pixels<=0;
         command_bytes<=0;command_bursts<=0;
         if(burst_s<1 || burst_s>32)begin error<=1;errcode<=1;state<=FINISH;end
         else begin
           burst_limit<=burst_s[5:0];
           if(pwdata==2)begin
             span_count<=0; // A new atlas invalidates every old row interval.
             valid_words<=0;load_fault<=0;load_index<=0;
             if(tex_src_s[1:0]!=0 || tex_src_s<32'h00400000 ||
                tex_src_s>32'h00bffffc || tex_words_s==0 || tex_words_s>4096 ||
                {1'b0,tex_src_s}+({1'b0,tex_words_s}<<2)>33'h00c00000)begin
               error<=1;errcode<=2;state<=FINISH;
             end else begin load_addr<=tex_src_s;load_left<=tex_words_s[12:0];state<=LPLAN;end
           end else if(pwdata==3)begin
             upload<=0;load_fault<=0;load_index<=0;
             if(count_s>512 || (count_s!=0 && (command_src_s[1:0]!=0 ||
                 command_src_s<32'h00400000 || command_src_s>32'h00bffffc ||
                 {1'b0,command_src_s}+({1'b0,count_s}<<4)>33'h00c00000)))begin
               error<=1;errcode<=9;state<=FINISH;
             end else if(count_s==0)state<=VALIDATE;
             else begin load_addr<=command_src_s;load_left<=count_s[9:0]<<2;state<=CPLAN;end
           end else if(pwdata==1)state<=VALIDATE;
           else begin error<=1;errcode<=1;state<=FINISH;end
         end
       end
     endcase
     if(writer_fault && state!=IDLE && state!=FINISH)begin error<=1;errcode<=6;state<=FINISH;end
     else case(state)
       VALIDATE:begin
         if(geometry_s[15:0]==0 || geometry_s[15:0]>640 || geometry_s[0] ||
            geometry_s[31:16]==0 || geometry_s[31:16]>480 ||
            stride_s<({16'd0,geometry_s[15:0]}<<1) || stride_s>65532 || stride_s[1:0]!=0 ||
            dst_s[1:0]!=0 || dst_s<32'h00400000 || dst_s>32'h00bffffc ||
            count_s>512 || (count_s<<2)>upload)begin error<=1;errcode<=3;state<=FINISH;end
         else begin
           dst<=dst_s;row_addr<=dst_s;stride<=stride_s[15:0];
           width<=geometry_s[9:0];height<=geometry_s[25:16];count<=count_s[9:0];
           background<=bg_s[15:0];row<=0;di<=0;clear_index<=0;
           compose_bank<=0;dual_enabled<=dual_enable_s;
           bg_ddr<=bg_s[30];bg_row_addr<=bg_src_s;
           bg_enabled<=bg_s[31];bg_origin<=bg_s[24:16];bg_base<=bg_s[12:0];
           state<=FRAME_SPAN;
         end
       end
       FRAME_SPAN:begin
         frame_span<=frame_product;
         bg_span<={16'd0,bg_s[15:0]}+((({23'd0,bg_origin}+{22'd0,height}+7)>>3)<<6);
         state<=FRAME_CHECK;
       end
       FRAME_CHECK:begin
         if({1'b0,dst}+frame_span+({23'd0,width}<<1)>33'h00c00000)begin
           error<=1;errcode<=3;state<=FINISH;
         end else if(bg_enabled && (bg_s[30:25]!=0 || bg_s[15:13]!=0 || bg_s[0] ||
             {1'b0,bg_origin}+{1'b0,height}>480 || bg_span>({19'd0,valid_words}<<1)))begin
           error<=1;errcode<=7;state<=FINISH;
         end else if(bg_ddr && (bg_s!=32'h40000000 || bg_src_s[1:0]!=0 ||
             bg_src_s<32'h00400000 || {1'b0,bg_src_s}+frame_span+({23'd0,width}<<1)>33'h00c00000 ||
             ({1'b0,bg_src_s}<{1'b0,dst}+frame_span+({23'd0,width}<<1) &&
              {1'b0,dst}<{1'b0,bg_src_s}+frame_span+({23'd0,width}<<1))))begin
           error<=1;errcode<=7;state<=FINISH;
         end else state<=count==0?(bg_ddr?BWAIT:bg_enabled?BGPRIME:CLEAR):VFETCH;
       end
       VFETCH:state<=VMUL;
       VMUL:begin span<=texture_product+{19'd0,d0[12:0]}+sw;state<=VCHECK;end
       VCHECK:begin
         if(sw==0 || sh==0 || sw>640 || sh>480 || {1'b0,sx}+sw>width ||
            {1'b0,sy}+sh>height || d0[29:16]<sw || d0[29:16]>8192 ||
            d0[14:13]!=0 || span>({19'd0,valid_words}<<1) ||
            (!d0[15] && (d0[31:30]!=0 || d3[31:26]!=0)))begin
           error<=1;errcode<=4;state<=FINISH;
         end else if(use_span && ({1'b0,span_base}+sh>span_count))begin
           error<=1;errcode<=8;state<=FINISH;
         end else if(di+1==count)begin di<=0;state<=bg_ddr?BWAIT:bg_enabled?BGPRIME:CLEAR;end
         else begin di<=di+1'b1;state<=VFETCH;end
       end
       CLEAR:begin
         if(clear_index+1==(width>>1))begin clear_index<=0;di<=0;write_word<=0;state<=count==0?WPLAN:FETCH;end
         else clear_index<=clear_index+1'b1;
       end
       BGPRIME:begin bg_column<=0;bg_repeat<=0;state<=BGCLEAR;end
       BGCLEAR:begin
         if(bg_repeat==4)begin bg_repeat<=0;bg_column<=bg_column+1'b1;end
         else bg_repeat<=bg_repeat+1'b1;
         if(clear_index+1==(width>>1))begin
           clear_index<=0;di<=0;write_word<=0;state<=count==0?WPLAN:FETCH;
         end else clear_index<=clear_index+1'b1;
       end
       FETCH:state<=SELECT;
       SELECT:begin
         descriptor_visits<=descriptor_visits+1'b1;
         if(row>=sy && row<({1'b0,sy}+sh))state<=ADDRESS;
         else if(di+1==count)begin write_word<=0;state<=WPLAN;end
         else begin di<=di+1'b1;state<=FETCH;end
       end
       ADDRESS:begin
         issue_x<=0;retired<=0;
         if(use_span && draw_first>=draw_end)begin
           skipped_pixels<=skipped_pixels+sw;
           if(di+1==count)begin write_word<=0;state<=WPLAN;end
           else begin di<=di+1'b1;state<=FETCH;end
         end else begin
           src_pixel<=draw_first[12:0];draw_offset<=draw_first-source_row;draw_width<=active_width[9:0];
           skipped_pixels<=skipped_pixels+({16'd0,sw}-active_width);state<=DRAW;
         end
       end
       DRAW:begin
         issue_x<=issue_x+1'b1;src_pixel<=src_pixel+1'b1;
         if(issue_x+1==draw_width)state<=DRAIN;
       end
       DRAIN:if(retired==draw_width)begin
         if(di+1==count)begin write_word<=0;state<=WPLAN;end
         else begin di<=di+1'b1;state<=FETCH;end
       end
       WPLAN:if(publish_row)state<=WA; // publish completed row to writer
       WA:state<=row+1==height?WD:WPRIME;
       WPRIME:if(!row_ready[!compose_bank] && (dual_enabled || (row_ready==0 && wx_state==WX_IDLE)))begin
         compose_bank<=!compose_bank;row<=row+1'b1;row_addr<=row_addr+stride;
         bg_row_addr<=bg_row_addr+stride;clear_index<=0;
         state<=bg_ddr?BWAIT:bg_enabled?BGPRIME:CLEAR;
       end
       WD:if(row_ready==0 && wx_state==WX_IDLE)state<=FINISH; // last row drained
       BWAIT:if(pf_state==PF_DONE)begin
         if(pf_fault)begin error<=1;errcode<=5;state<=FINISH;end
         else state<=BCPRIME;
       end
       BCPRIME:state<=BGCOPY;
       BGCOPY:begin
         if(clear_index+1==(width>>1))begin
           clear_index<=0;di<=0;write_word<=0;state<=count==0?WPLAN:FETCH;
         end else clear_index<=clear_index+1'b1;
       end
       LPLAN:begin beats<=planned;beat<=0;state<=LA;end
       LA:if(arready)state<=LD;
       LD:if(rvalid)begin
         read_bytes<=read_bytes+4;load_index<=load_index+1'b1;beat<=beat+1'b1;
         if(rresp!=0 || rlast!=(beat==beats-1))load_fault<=1;
         if(rlast)begin
           if(load_fault || rresp!=0 || beat!=beats-1)begin error<=1;errcode<=5;state<=FINISH;end
           else if(load_left==beats)begin valid_words<=tex_words_s[12:0];state<=FINISH;end
           else begin load_left<=load_left-beats;load_addr<=load_addr+({26'd0,beats}<<2);state<=LPLAN;end
         end
       end
       CPLAN:begin beats<=planned;beat<=0;state<=CA;end
       CA:if(arready)begin command_bursts<=command_bursts+1'b1;state<=CD;end
       CD:if(rvalid)begin
         read_bytes<=read_bytes+4;command_bytes<=command_bytes+4;
         load_index<=load_index+1'b1;beat<=beat+1'b1;
         if(rresp!=0 || rlast!=(beat==beats-1))load_fault<=1;
         // Drain the accepted burst even if an earlier beat had RRESP error.
         if(rlast)begin
           if(load_fault || rresp!=0 || beat!=beats-1)begin error<=1;errcode<=5;state<=FINISH;end
           else if(load_left==beats)begin upload<=count_s[9:0]<<2;state<=VALIDATE;end
           else begin load_left<=load_left-beats;load_addr<=load_addr+({26'd0,beats}<<2);state<=CPLAN;end
         end
       end
       FINISH:if((pf_state==PF_IDLE || pf_state==PF_DONE) && wx_state==WX_IDLE)begin state<=IDLE;done<=1;complete_seq<=seq_s;end
     endcase
   end
 end
endmodule
`default_nettype wire
