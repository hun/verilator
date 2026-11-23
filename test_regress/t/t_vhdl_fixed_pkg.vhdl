-- DESCRIPTION: Verilator: VHDL ieee.fixed_pkg sfixed and ufixed
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Arithmetic, comparison, resize and conversions of the fixed-point types. The
-- C++ driver applies every operand pair and hashes the outputs; the expected
-- hash comes from the same entity simulated with GHDL 7.0.0-dev (--std=08).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.fixed_float_types.all;
use ieee.fixed_pkg.all;

entity t is
  port (
    sa : in sfixed(3 downto -2);
    sb : in sfixed(2 downto -1);
    ua : in ufixed(3 downto -2);
    ub : in ufixed(2 downto -1);
    s_add, s_sub : out sfixed(4 downto -2);
    s_mul : out sfixed(6 downto -3);
    s_neg, s_abs : out sfixed(4 downto -2);
    u_add, u_sub : out ufixed(4 downto -2);
    u_mul : out ufixed(6 downto -3);
    s_lt, s_le, s_eq, s_gt : out std_logic;
    u_lt, u_le, u_eq, u_gt : out std_logic;
    s_r1 : out sfixed(2 downto -1);     -- narrow: saturate, round
    s_r2 : out sfixed(2 downto -1);     -- wrap, round
    s_r3 : out sfixed(2 downto -1);     -- saturate, truncate
    s_r4 : out sfixed(2 downto -1);     -- wrap, truncate
    s_r5 : out sfixed(5 downto -4);     -- widen
    s_r6 : out sfixed(1 downto 0);      -- drop fraction entirely
    s_r7 : out sfixed(-1 downto -3);    -- below the point
    u_r1 : out ufixed(2 downto -1);
    u_r2 : out ufixed(2 downto -1);
    u_r3 : out ufixed(2 downto -1);
    u_r4 : out ufixed(2 downto -1);
    u_r5 : out ufixed(5 downto -4);
    u_r6 : out ufixed(1 downto 0);
    u_r7 : out ufixed(-1 downto -3);
    s_slv : out std_logic_vector(5 downto 0);
    s_conv : out sfixed(3 downto -2);
    s_int : out integer;
    u_int : out natural;
    s_sgn : out signed(3 downto 0);
    u_uns : out unsigned(3 downto 0);
    s_mixi : out sfixed(3 downto -2);
    s_mixr : out sfixed(3 downto -2);
    u_toi : out ufixed(4 downto -2);
    s_toi : out sfixed(4 downto -2)
  );
end;

architecture rtl of t is
begin
  s_add <= sa + sb;
  s_sub <= sa - sb;
  s_mul <= sa * sb;
  s_neg <= -sa;
  s_abs <= abs sa;
  u_add <= ua + ub;
  u_sub <= ua - ub;
  u_mul <= ua * ub;
  s_lt <= '1' when sa < sb else '0';
  s_le <= '1' when sa <= sb else '0';
  s_eq <= '1' when sa = sb else '0';
  s_gt <= '1' when sa > sb else '0';
  u_lt <= '1' when ua < ub else '0';
  u_le <= '1' when ua <= ub else '0';
  u_eq <= '1' when ua = ub else '0';
  u_gt <= '1' when ua > ub else '0';
  s_r1 <= resize(sa, 2, -1);
  s_r2 <= resize(sa, 2, -1, fixed_wrap, fixed_round);
  s_r3 <= resize(sa, 2, -1, fixed_saturate, fixed_truncate);
  s_r4 <= resize(sa, 2, -1, fixed_wrap, fixed_truncate);
  s_r5 <= resize(sa, 5, -4);
  s_r6 <= resize(sa, 1, 0);
  s_r7 <= resize(sa, -1, -3);
  u_r1 <= resize(ua, 2, -1);
  u_r2 <= resize(ua, 2, -1, fixed_wrap, fixed_round);
  u_r3 <= resize(ua, 2, -1, fixed_saturate, fixed_truncate);
  u_r4 <= resize(ua, 2, -1, overflow_style => fixed_wrap, round_style => fixed_truncate);
  u_r5 <= resize(ua, 5, -4);
  u_r6 <= resize(ua, 1, 0);
  u_r7 <= resize(ua, -1, -3);
  s_slv <= to_slv(sa);
  s_conv <= to_sfixed(to_slv(sa), 3, -2);
  s_int <= to_integer(sa);
  u_int <= to_integer(ua);
  s_sgn <= to_signed(sa, 4);
  u_uns <= to_unsigned(ua, 4);
  s_mixi <= resize(sa + 1, 3, -2);
  s_mixr <= resize(sa * 1.5, 3, -2);
  u_toi <= to_ufixed(to_unsigned(ua, 4), 4, -2);
  s_toi <= to_sfixed(to_signed(sa, 4), 4, -2);
end;
