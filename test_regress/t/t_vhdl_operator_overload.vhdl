-- DESCRIPTION: Verilator: VHDL operator and array-type overload resolution
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    a0     : in integer;
    a1     : in integer;
    b0     : in integer;
    b1     : in integer;
    u      : in unsigned(3 downto 0);
    s      : in signed(3 downto 0);
    sum0   : out integer;
    sum1   : out integer;
    diff0  : out integer;
    diff1  : out integer;
    pick_u : out integer;
    pick_s : out integer;
    pick_a : out integer;
    pick_b : out integer;
    pick_f : out integer
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

  signal pa, pb, ps : pair_t;
  signal qa, qb, qs : pair2_t;
begin
  pa <= (a0, a1);
  pb <= (b0, b1);
  qa <= (a0, a1);
  qb <= (b0, b1);
  -- Same operator symbol, selected by the operand array type
  ps <= pa + pb;
  qs <= qa + qb;
  sum0 <= ps(0);
  sum1 <= ps(1);
  diff0 <= qs(0);
  diff1 <= qs(1);
  -- Same function name, selected by the full parameter type
  pick_u <= pick(u);
  pick_s <= pick(s);
  pick_a <= pick(pa);
  pick_b <= pick(qa);
  pick_f <= pick(make(1));
end;
