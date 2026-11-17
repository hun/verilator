-- DESCRIPTION: Verilator: VHDL operator and array-type overload resolution
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package ov_a_pkg is
  type pair_t is array (0 to 1) of integer;
end package;

package ov_b_pkg is
  type pair_t is array (0 to 1) of integer;
end package;

entity t is
  port (
    a0     : in integer;
    a1     : in integer;
    b0     : in integer;
    b1     : in integer;
    u      : in unsigned(3 downto 0);
    s      : in signed(3 downto 0);
    sv     : in std_logic_vector(3 downto 0);
    sum0   : out integer;
    sum1   : out integer;
    diff0  : out integer;
    diff1  : out integer;
    neg0   : out integer;
    neg1   : out integer;
    bit_o  : out std_logic_vector(3 downto 0);
    pick_u : out integer;
    pick_s : out integer;
    pick_a : out integer;
    pick_b : out integer;
    pick_f : out integer;
    pick_pa : out integer;
    pick_pb : out integer
  );
end;

architecture rtl of t is
  type pair_t is array (0 to 1) of integer;
  type pair2_t is array (0 to 1) of integer;

  function "+"(l, r : pair_t) return pair_t is
  begin
    return (l(0) + r(0) + 100, l(1) + r(1) + 100);
  end function;

  function "+"(l, r : pair2_t) return pair2_t is
  begin
    return (l(0) - r(0), l(1) - r(1));
  end function;

  function "-"(x : pair_t) return pair_t is
  begin
    return (-x(0), -x(1) - 100);
  end function;

  function "xor"(l, r : std_logic_vector(3 downto 0)) return std_logic_vector(3 downto 0) is
  begin
    return "1010";
  end function;

  function pick(x : unsigned(3 downto 0)) return integer is
  begin
    return 1;
  end function;

  function pick(x : signed(3 downto 0)) return integer is
  begin
    return 2;
  end function;

  function pick(x : pair_t) return integer is
  begin
    return 3;
  end function;

  function pick(x : pair2_t) return integer is
  begin
    return 4;
  end function;

  function make(v : integer) return pair_t is
  begin
    return (v, v + 1);
  end function;

  function pickp(x : work.ov_a_pkg.pair_t) return integer is
  begin
    return 5;
  end function;

  function pickp(x : work.ov_b_pkg.pair_t) return integer is
  begin
    return 6;
  end function;

  signal pa, ps : pair_t;
  signal ns : pair_t;
  signal qa, qb, qs : pair2_t;
  signal xa : work.ov_a_pkg.pair_t;
  signal xb : work.ov_b_pkg.pair_t;
begin
  pa <= (a0, a1);
  ns <= -pa;
  qa <= (a0, a1);
  qb <= (b0, b1);
  xa <= (a0, a1);
  xb <= (b0, b1);
  -- Same operator symbol, selected by the operand array type
  ps <= pa + (b0, b1);
  qs <= qa + qb;
  sum0 <= ps(0);
  sum1 <= ps(1);
  diff0 <= qs(0);
  diff1 <= qs(1);
  neg0 <= ns(0);
  neg1 <= ns(1);
  -- A bit-string literal takes its type from the selected operator's formal
  bit_o <= sv xor "0001";
  -- Same function name, selected by the full parameter type
  pick_u <= pick(u);
  pick_s <= pick(s);
  pick_a <= pick(pa);
  pick_b <= pick(qa);
  pick_f <= pick(make(1));
  -- Same-named types from two packages stay distinct
  pick_pa <= pickp(xa);
  pick_pb <= pickp(xb);
end;
