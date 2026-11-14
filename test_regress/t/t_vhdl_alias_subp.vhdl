-- DESCRIPTION: Verilator: VHDL alias declarations that rename a subprogram
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- Every alias below names one of package p's functions through a signature, so a call of
-- the alias resolves like a call of the function itself: in p's own declarative part
-- (add5, dbl), in a package that uses p (qadd, so the synonym has to survive publication
-- and import), in an architecture (add_local, whose result a constant uses), and in a
-- subprogram's own declarative part (twice2 naming a package function, plus2 naming a
-- subprogram of the same declarative part). The rmin alias is the shape poc's utils.vhdl
-- uses: a standard-package function, renamed but not called, as a real object is not
-- modelled yet.

library ieee;
use ieee.std_logic_1164.all;
use ieee.math_real.all;

package p is
  function add(value : integer; delta : integer) return integer;
  function twice(value : integer) return integer;
  function twice(value : integer; delta : integer) return integer;

  alias add5 is add[integer, integer return integer];
  alias dbl is twice[integer return integer];
  alias rmin is IEEE.math_real.realmin[real, real return real];

  function inside(value : integer) return integer;
  function nested(value : integer) return integer;
end package p;

package body p is
  function add(value : integer; delta : integer) return integer is
  begin
    return value + delta;
  end function add;

  function twice(value : integer) return integer is
  begin
    return 2 * value;
  end function twice;

  function twice(value : integer; delta : integer) return integer is
  begin
    return 2 * (value + delta);
  end function twice;

  function inside(value : integer) return integer is
    alias twice2 is twice[integer, integer return integer];
  begin
    return twice2(value, 1);
  end function inside;

  function nested(value : integer) return integer is
    function plus(left : integer; right : integer) return integer is
    begin
      return left + right;
    end function plus;
    alias plus2 is plus[integer, integer return integer];
  begin
    return plus2(value, 3);
  end function nested;
end package body p;

package q is
  alias qadd is work.p.add[integer, integer return integer];
end package q;

library ieee;
use ieee.std_logic_1164.all;
use work.p.all;
use work.q.all;

entity t is
  port (
    i_a      : in  integer;
    o_sum    : out integer;
    o_dbl    : out integer;
    o_q      : out integer;
    o_const  : out integer;
    o_inside : out integer;
    o_nested : out integer
  );
end entity t;

architecture rtl of t is
  alias add_local is work.p.add[integer, integer return integer];
  constant base : integer := add_local(2, 3);
begin
  o_sum    <= add5(i_a, base);
  o_dbl    <= dbl(i_a);
  o_q      <= qadd(i_a, 10);
  o_const  <= base;
  o_inside <= inside(i_a);
  o_nested <= nested(i_a);
end architecture rtl;
