// AI-assisted stage 24 wrapper. Legacy 0x00..0x7f, scanline 0x80..0xff.
`timescale 1ns/1ps
`default_nettype none
module fs_blitter(
 input wire clk, input wire reset,
 input wire [15:0] paddr, input wire psel,penable,pwrite,
 input wire [31:0] pwdata, output reg [31:0] prdata,
 output wire pready, output wire pslverror,
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
// Exclusive AXI ownership; a core is idle only after outstanding responses drain.
wire scan_busy,legacy_busy;
wire scan_selected=paddr[7];
wire reject=psel && penable && pwrite && ((scan_selected && legacy_busy)||(!scan_selected && scan_busy));
wire [31:0] l_prdata;
wire  l_pready;
wire  l_pslverror;
wire [31:0] l_awaddr;
wire [7:0] l_awlen;
wire [2:0] l_awsize;
wire [1:0] l_awburst;
wire  l_awvalid;
wire  l_awready;
wire [31:0] l_wdata;
wire [3:0] l_wstrb;
wire  l_wlast;
wire  l_wvalid;
wire  l_wready;
wire [1:0] l_bresp;
wire  l_bvalid;
wire  l_bready;
wire [31:0] l_araddr;
wire [7:0] l_arlen;
wire [2:0] l_arsize;
wire [1:0] l_arburst;
wire  l_arvalid;
wire  l_arready;
wire [31:0] l_rdata;
wire [1:0] l_rresp;
wire  l_rlast;
wire  l_rvalid;
wire  l_rready;
wire [31:0] s_prdata;
wire  s_pready;
wire  s_pslverror;
wire [31:0] s_awaddr;
wire [7:0] s_awlen;
wire [2:0] s_awsize;
wire [1:0] s_awburst;
wire  s_awvalid;
wire  s_awready;
wire [31:0] s_wdata;
wire [3:0] s_wstrb;
wire  s_wlast;
wire  s_wvalid;
wire  s_wready;
wire [1:0] s_bresp;
wire  s_bvalid;
wire  s_bready;
wire [31:0] s_araddr;
wire [7:0] s_arlen;
wire [2:0] s_arsize;
wire [1:0] s_arburst;
wire  s_arvalid;
wire  s_arready;
wire [31:0] s_rdata;
wire [1:0] s_rresp;
wire  s_rlast;
wire  s_rvalid;
wire  s_rready;
always @* prdata=scan_selected?s_prdata:l_prdata;
assign pready=1'b1;
assign pslverror=reject|l_pslverror|s_pslverror;
assign awaddr=scan_busy?s_awaddr:l_awaddr;
assign awlen=scan_busy?s_awlen:l_awlen;
assign awsize=scan_busy?s_awsize:l_awsize;
assign awburst=scan_busy?s_awburst:l_awburst;
assign awvalid=scan_busy?s_awvalid:l_awvalid;
assign wdata=scan_busy?s_wdata:l_wdata;
assign wstrb=scan_busy?s_wstrb:l_wstrb;
assign wlast=scan_busy?s_wlast:l_wlast;
assign wvalid=scan_busy?s_wvalid:l_wvalid;
assign bready=scan_busy?s_bready:l_bready;
assign araddr=scan_busy?s_araddr:l_araddr;
assign arlen=scan_busy?s_arlen:l_arlen;
assign arsize=scan_busy?s_arsize:l_arsize;
assign arburst=scan_busy?s_arburst:l_arburst;
assign arvalid=scan_busy?s_arvalid:l_arvalid;
assign rready=scan_busy?s_rready:l_rready;
assign l_awready=awready && !scan_busy;
assign l_wready=wready && !scan_busy;
assign l_bresp=bresp;
assign l_bvalid=bvalid && !scan_busy;
assign l_arready=arready && !scan_busy;
assign l_rdata=rdata;
assign l_rresp=rresp;
assign l_rlast=rlast;
assign l_rvalid=rvalid && !scan_busy;
assign s_awready=awready && scan_busy;
assign s_wready=wready && scan_busy;
assign s_bresp=bresp;
assign s_bvalid=bvalid && scan_busy;
assign s_arready=arready && scan_busy;
assign s_rdata=rdata;
assign s_rresp=rresp;
assign s_rlast=rlast;
assign s_rvalid=rvalid && scan_busy;
fs_blitter_legacy u_legacy(
 .clk(clk),
 .reset(reset),
 .paddr(paddr),
 .psel(psel && !scan_selected && !scan_busy),
 .penable(penable),
 .pwrite(pwrite),
 .pwdata(pwdata),
 .prdata(l_prdata),
 .pready(l_pready),
 .pslverror(l_pslverror),
 .awaddr(l_awaddr),
 .awlen(l_awlen),
 .awsize(l_awsize),
 .awburst(l_awburst),
 .awvalid(l_awvalid),
 .awready(l_awready),
 .wdata(l_wdata),
 .wstrb(l_wstrb),
 .wlast(l_wlast),
 .wvalid(l_wvalid),
 .wready(l_wready),
 .bresp(l_bresp),
 .bvalid(l_bvalid),
 .bready(l_bready),
 .araddr(l_araddr),
 .arlen(l_arlen),
 .arsize(l_arsize),
 .arburst(l_arburst),
 .arvalid(l_arvalid),
 .arready(l_arready),
 .rdata(l_rdata),
 .rresp(l_rresp),
 .rlast(l_rlast),
 .rvalid(l_rvalid),
 .rready(l_rready),
 .busy_out(legacy_busy)
);
fs_scanline u_scan(
 .clk(clk),
 .reset(reset),
 .paddr(paddr),
 .psel(psel && scan_selected && !legacy_busy),
 .penable(penable),
 .pwrite(pwrite),
 .pwdata(pwdata),
 .prdata(s_prdata),
 .pready(s_pready),
 .pslverror(s_pslverror),
 .awaddr(s_awaddr),
 .awlen(s_awlen),
 .awsize(s_awsize),
 .awburst(s_awburst),
 .awvalid(s_awvalid),
 .awready(s_awready),
 .wdata(s_wdata),
 .wstrb(s_wstrb),
 .wlast(s_wlast),
 .wvalid(s_wvalid),
 .wready(s_wready),
 .bresp(s_bresp),
 .bvalid(s_bvalid),
 .bready(s_bready),
 .araddr(s_araddr),
 .arlen(s_arlen),
 .arsize(s_arsize),
 .arburst(s_arburst),
 .arvalid(s_arvalid),
 .arready(s_arready),
 .rdata(s_rdata),
 .rresp(s_rresp),
 .rlast(s_rlast),
 .rvalid(s_rvalid),
 .rready(s_rready),
 .busy(scan_busy)
);
endmodule
`default_nettype wire
