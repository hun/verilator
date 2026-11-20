-- DESCRIPTION: Verilator: VHDL std_logic_arith conversion builtins
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
-- Deliberately only std_logic_arith: mixing it with numeric_std is ambiguous
use ieee.std_logic_arith.all;

entity t is
  port (
    u : out std_logic_vector(7 downto 0);
    s : out std_logic_vector(7 downto 0);
    w : out unsigned(7 downto 0);
    x : out signed(7 downto 0);
    v : in  unsigned(7 downto 0);
    n : in  integer range 0 to 9;
    sl : out unsigned(7 downto 0);
    sr : out unsigned(7 downto 0);
    u4 : in  unsigned(3 downto 0);
    u3 : in  unsigned(2 downto 0);
    s4 : in  signed(3 downto 0);
    s3 : in  signed(2 downto 0);
    i  : in  integer range -16 to 15;
    lt_uu : out boolean;
    lt_ss : out boolean;
    lt_us : out boolean;
    eq_us : out boolean;
    lt_su : out boolean;
    lt_ui : out boolean;
    lt_iu : out boolean;
    eq_ui : out boolean;
    lt_si : out boolean;
    lt_is : out boolean;
    eq_si : out boolean
  );
end;

architecture rtl of t is
begin
  u <= conv_std_logic_vector(5, 8);
  s <= conv_std_logic_vector(-1, 8);
  w <= conv_unsigned(5, 8);
  x <= conv_signed(-1, 8);
  sl <= shl(v, n);
  sr <= shr(v, n);
  lt_uu <= u4 < u3;
  lt_ss <= s4 < s3;
  lt_us <= u4 < s4;
  eq_us <= u4 = s4;
  lt_su <= s4 < u4;
  lt_ui <= u4 < i;
  lt_iu <= i < u4;
  eq_ui <= u4 = i;
  lt_si <= s4 < i;
  lt_is <= i < s4;
  eq_si <= s4 = i;
end;
