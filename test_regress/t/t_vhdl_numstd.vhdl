-- DESCRIPTION: Verilator: VHDL numeric_std shift, rotate, minimum and maximum
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    u     : in unsigned(3 downto 0);
    s     : in signed(3 downto 0);
    i     : in integer range 0 to 7;
    sl_o  : out unsigned(3 downto 0);
    sr_o  : out unsigned(3 downto 0);
    rl_o  : out unsigned(3 downto 0);
    rr_o  : out unsigned(3 downto 0);
    ssr_o : out signed(3 downto 0);
    min_o : out unsigned(3 downto 0);
    max_o : out unsigned(3 downto 0);
    smin_o : out signed(3 downto 0);
    smax_o : out signed(3 downto 0)
  );
end;

architecture rtl of t is
begin
  sl_o <= shift_left(u, i);
  sr_o <= shift_right(u, i);
  rl_o <= rotate_left(u, i);
  rr_o <= rotate_right(u, i);
  ssr_o <= shift_right(s, i);
  min_o <= minimum(u, (u - 1));
  max_o <= maximum(u, (u - 1));
  smin_o <= minimum(s, (s + 1));
  smax_o <= maximum(s, (s + 1));
end;
