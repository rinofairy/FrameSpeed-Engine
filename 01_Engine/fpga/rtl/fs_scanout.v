// RGB565 640x480 scanout. Two 320-word line banks with mailbox CDC.
// Pixel clock 25.2 MHz: 800x525 => 60 Hz. No clock derived in logic.
// A late/failed line is black for its whole duration, never shifted pixels.
`default_nettype none
module fs_scanout(
 input wire clk,reset,pclk,preset,
 input wire [15:0] paddr,input wire psel,penable,pwrite,input wire [31:0] pwdata,
 output reg [31:0] prdata,output wire pslverror,
 output reg [31:0] araddr,output wire [7:0] arlen,output wire arvalid,input wire arready,
 input wire [31:0] rdata,input wire [1:0] rresp,input wire rlast,rvalid,output wire rready,
 output reg hs,vs,de,output wire [23:0] rgb
);
 reg [31:0] front,next_base,next_seq,accepted_seq,scan_count,display_count,underruns,axi_errors;
 reg enabled,pending; reg [31:0] pending_base,pending_seq;
 wire wr=psel && penable && pwrite;
 wire valid_base=next_base[11:0]==0 && next_base>=32'h400000 && next_base<=32'hb69000;
 assign pslverror=wr && paddr==16'h110 && (pending || !valid_base);
 always @* begin
  prdata=0;
  case(paddr)
   'h100:prdata=32'h46534401;'h104:prdata={30'd0,pending,enabled};
   'h108:prdata=next_base;'h10c:prdata=next_seq;'h114:prdata=accepted_seq;
   'h118:prdata=front;'h11c:prdata=scan_count;'h120:prdata=display_count;
   'h124:prdata=underruns;'h128:prdata=axi_errors;
  endcase
 end
 // Pixel -> core mailbox. Payload stays stable until returned ack.
 reg req;reg [8:0] req_row;reg [15:0] req_epoch;reg req_frame;
 reg [31:0] req_scans,req_shown,req_under;
 (* async_reg="true" *) reg req_s1,req_s2;
 reg ack;reg [8:0] done_row;reg [15:0] done_epoch;reg done_ok;reg [31:0] done_seq;
 (* async_reg="true" *) reg ack_s1,ack_s2;
 reg ack_seen;
 reg [31:0] lines[0:1023];
 reg [2:0] state;reg [8:0] fetch_row,word_index;reg [15:0] fetch_epoch;
 reg [4:0] beat;reg fault;reg [31:0] fetch_seq;
 assign arlen=8'd15;assign arvalid=state==2;assign rready=state==3;
 // Starts are 4KB aligned, line stride 1280 is a multiple of 64, so
 // every fixed 64-byte burst stays inside its 4KB page.
 always @(posedge clk) begin
  req_s1<=req;req_s2<=req_s1;
  if(reset) begin
   req_s1<=0;req_s2<=0;ack<=0;state<=0;front<=32'h800000;next_base<=32'h900000;
   next_seq<=0;pending<=0;enabled<=0;pending_base<=0;pending_seq<=0;accepted_seq<=0;
   scan_count<=0;display_count<=0;underruns<=0;axi_errors<=0;
   araddr<=0;fetch_row<=0;fetch_epoch<=0;word_index<=0;beat<=0;fault<=0;fetch_seq<=0;
   done_row<=0;done_epoch<=0;done_ok<=0;done_seq<=0;
  end else begin
   if(wr) case(paddr)
    'h108:next_base<=pwdata;'h10c:next_seq<=pwdata;
    'h110:if(!pslverror)begin pending<=1;pending_base<=next_base;pending_seq<=next_seq;end
   endcase
   case(state)
    0: if(req_s2!=ack)begin
     fetch_row<=req_row;fetch_epoch<=req_epoch;word_index<=0;beat<=0;fault<=0;
     if(req_frame)begin
      scan_count<=req_scans;display_count<=req_shown;underruns<=req_under;
      if(pending)begin front<=pending_base;accepted_seq<=pending_seq;pending<=0;enabled<=1;end
     end
     state<=1;
    end
    1: begin
     araddr<=front+({23'd0,fetch_row}<<10)+({23'd0,fetch_row}<<8);
     fetch_seq<=accepted_seq;
     if(enabled)state<=2;else begin fault<=1;state<=4;end
    end
    2:if(arready)begin beat<=0;state<=3;end
    3:if(rvalid)begin
     if(beat<16) lines[{fetch_row[0],word_index}]<=rdata;
     if(rresp!=0 || rlast!=(beat==15))fault<=1;
     if(rlast)begin
      if(fault || rresp!=0 || beat!=15)begin axi_errors<=axi_errors+1;state<=4;end
      else if(word_index==319)state<=4;
      else begin araddr<=araddr+64;state<=2;end
     end
     if(beat<31)beat<=beat+1;
     if(beat<16)word_index<=word_index+1;
    end
    4:begin done_row<=fetch_row;done_epoch<=fetch_epoch;done_ok<=!fault;
      done_seq<=fetch_seq;ack<=req_s2;state<=0;end
   endcase
  end
 end
 reg [9:0] x,y;reg [15:0] epoch;
 reg [8:0] bank_row0,bank_row1;reg [15:0] bank_epoch0,bank_epoch1;
 reg bank_ok0,bank_ok1;reg [31:0] bank_seq0,bank_seq1;
 reg row_ok,frame_ok,video_started;reg [31:0] frame_seq,last_shown,scans,shown,under;
 wire ready_row=y[0]?(bank_ok1 && bank_row1==y && bank_epoch1==epoch):
                           (bank_ok0 && bank_row0==y && bank_epoch0==epoch);
 wire good_pixel=x==0?ready_row:row_ok;
 reg [31:0] line_word;reg half_pixel,pixel_ok;
 wire [15:0] pixel=half_pixel?line_word[31:16]:line_word[15:0];
 assign rgb=de && pixel_ok ? {pixel[15:11],pixel[15:13],pixel[10:5],pixel[10:9],pixel[4:0],pixel[4:2]} : 24'd0;
 always @(posedge pclk) begin
  ack_s1<=ack;ack_s2<=ack_s1;
  // BRAM synchronous read, matched by one cycle of DE/HS/VS delay.
  line_word<=lines[{y[0],x[9:1]}];half_pixel<=x[0];
  if(preset)begin
   x<=0;y<=480;epoch<=0;req<=0;req_row<=0;req_epoch<=0;req_frame<=0;
   req_scans<=0;req_shown<=0;req_under<=0;ack_s1<=0;ack_s2<=0;ack_seen<=0;
   bank_ok0<=0;bank_ok1<=0;bank_row0<=0;bank_row1<=0;bank_epoch0<=0;bank_epoch1<=0;
   bank_seq0<=0;bank_seq1<=0;row_ok<=0;frame_ok<=0;frame_seq<=0;last_shown<=0;
   scans<=0;shown<=0;under<=0;hs<=1;vs<=1;de<=0;pixel_ok<=0;video_started<=0;
  end else begin
   hs<=!(x>=656 && x<752);vs<=!(y>=490 && y<492);
   de<=x<640 && y<480;pixel_ok<=good_pixel;
   if(x==799)begin x<=0;y<=y==524?0:y+1;end else x<=x+1;
   if(ack_s2!=ack_seen)begin
    ack_seen<=ack_s2;
    if(done_ok && done_seq!=0)video_started<=1;
    if(done_row[0])begin bank_row1<=done_row;bank_epoch1<=done_epoch;bank_ok1<=done_ok;bank_seq1<=done_seq;end
    else begin bank_row0<=done_row;bank_epoch0<=done_epoch;bank_ok0<=done_ok;bank_seq0<=done_seq;end
   end
   if(x==0 && y<480)begin
    row_ok<=ready_row;
    if(video_started && !ready_row)under<=under+1;
    if(y==0)begin frame_ok<=ready_row;frame_seq<=bank_seq0;end
    else if(!ready_row)frame_ok<=0;
   end
   if(x==0 && y==480)begin
    scans<=scans+1;epoch<=epoch+1;
    if(frame_ok && frame_seq!=last_shown)begin shown<=shown+1;last_shown<=frame_seq;end
   end
   // Prefetch row zero during vertical blank; prefetch next row while
   // current row uses the other bank. Never overwrite an outstanding request.
   if(x==0 && (y==480 || y<479) && req==ack_s2)begin
    req<=!req;req_row<=y==480?0:y+1;req_epoch<=y==480?epoch+1:epoch;
    req_frame<=y==480;req_scans<=scans;req_shown<=shown;req_under<=under;
   end
  end
 end
endmodule
`default_nettype wire
