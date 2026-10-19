// Real MIPI video source: two BGR888 pixels/clock, 1920x1080 timings
// inherited from V4 J5 reference. 32x16 text, 4x scaled 6x8 glyphs.
// Double-buffered text RAM: CPU commits a whole page at vertical blank.
`default_nettype none
module fs_dashboard(
 input wire clk,reset,input wire [15:0] paddr,input wire psel,penable,pwrite,
 input wire [31:0] pwdata,output wire [31:0] prdata,output wire pslverror,
 input wire pclk,preset,output reg hs,vs,de,output wire [47:0] pixels
);
 reg req,ack;(* async_reg="true" *)reg a1,a2,q1,q2;
 wire busy=req!=a2;wire wr=psel && penable && pwrite;
 wire text_write=wr && paddr[15:11]==5'b00001;
 assign prdata=paddr==16'h200 ? 32'h46534d01 : paddr==16'h204 ? {31'd0,busy} : 32'd0;
 assign pslverror=wr && busy && (text_write || paddr==16'h208);
 reg [7:0] text_a[0:1023],text_b[0:1023];
 always @(posedge clk)begin
  a1<=ack;a2<=a1;
  if(reset)begin req<=0;a1<=0;a2<=0;end
  else begin
   if(text_write && !busy)begin text_a[{!req,paddr[10:2]}]<=pwdata[7:0];text_b[{!req,paddr[10:2]}]<=pwdata[7:0];end
   if(wr && paddr==16'h208 && !busy)req<=!req;
  end
 end
 reg [10:0] x;reg [10:0] y;reg active_bank,started;
 wire inside=x>=576 && x<1344 && y>=284 && y<796;
 wire [10:0] tx=x-576,ty=y-284;
 // 24 pixel cell width; constant division is optimized by synthesis.
 wire [4:0] col=tx/24;wire [3:0] row=ty[8:5];
 wire [2:0] gx0=(tx%24)>>2,gx1=((tx+1)%24)>>2;
 reg [7:0] ch0,ch1;reg [2:0] gy_d,gx0_d,gx1_d;
 reg inside_d,hs_d,vs_d,de_d;
 reg [7:0] font[0:1023];initial $readmemh("fs_font.hex",font);
 reg [7:0] bits0,bits1;reg [2:0] gx0_dd,gx1_dd;reg inside_dd;
 wire on0=inside_dd && gx0_dd<5 && bits0[4-gx0_dd];
 wire on1=inside_dd && gx1_dd<5 && bits1[4-gx1_dd];
 // BGR order, matching vendor pixcel_122 input {blue,green,red}.
 assign pixels={on1?24'he8ffe8:24'h201810,on0?24'he8ffe8:24'h201810};
 always @(posedge pclk)begin
  q1<=req;q2<=q1;
  ch0<=text_a[{active_bank,row,col}];ch1<=text_b[{active_bank,row,col}];
  gy_d<=ty[4:2];gx0_d<=gx0;gx1_d<=gx1;
  inside_d<=inside && started;
  bits0<=font[{ch0[6:0],gy_d}];bits1<=font[{ch1[6:0],gy_d}];
  gx0_dd<=gx0_d;gx1_dd<=gx1_d;inside_dd<=inside_d;
  hs_d<=x>=2040 && x<2042;vs_d<=y>=1100 && y<1102;de_d<=x<1920 && y<1080;
  hs<=hs_d;vs<=vs_d;de<=de_d;
  if(preset)begin x<=0;y<=1080;q1<=0;q2<=0;ack<=0;active_bank<=0;started<=0;de<=0;de_d<=0;end
  else begin
   if(x==2128)begin x<=0;y<=y==1121?0:y+1;end else x<=x+2;
   if(x==0 && y==1080 && q2!=ack)begin active_bank<=q2;ack<=q2;started<=1;end
  end
 end
endmodule
`default_nettype wire
