-- DESCRIPTION: Verilator: VHDL subprogram formal of an unconstrained array type
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A formal of an unconstrained array type denotes the actual, so its elements are the
-- actual's elements (not bits of them), its 'range and 'length are the actual's bounds,
-- and a variable declared with its 'subtype follows the actual's direction as well. Both
-- index directions are exercised: `data` counts up, `rdata` counts down.

package p is
  type vec_t is array (natural range <>) of integer;
  function double(vec : vec_t; index : integer) return integer;
  function total(vec : vec_t) return integer;
  function width(vec : vec_t) return integer;
  function moved(vec : vec_t; index : integer) return integer;
end package p;

package body p is
  function double(vec : vec_t; index : integer) return integer is
  begin
    return vec(index) * 2;
  end function double;

  function total(vec : vec_t) return integer is
    variable sum : integer := 0;
  begin
    for i in vec'range loop
      sum := sum + vec(i);
    end loop;
    return sum;
  end function total;

  function width(vec : vec_t) return integer is
  begin
    return vec'length * 3;
  end function width;

  function moved(vec : vec_t; index : integer) return integer is
    variable copy : vec'subtype;
  begin
    copy(index) := vec(index) + 1;
    return copy(index);
  end function moved;
end package body p;

library ieee;
use ieee.std_logic_1164.all;
use work.p.all;

entity t is
  port (
    i_up : in  std_ulogic;
    o_a  : out integer;
    o_b  : out integer;
    o_c  : out integer;
    o_d  : out integer
  );
end entity t;

architecture rtl of t is
  constant data : vec_t(0 to 3) := (10, 20, 30, 40);
  constant rdata : vec_t(3 downto 0) := (10, 20, 30, 40);
begin
  process (i_up)
  begin
    if i_up = '1' then
      o_a <= double(data, 1);
      o_b <= total(data);
      o_c <= width(data);
      o_d <= moved(rdata, 3);
    else
      o_a <= double(rdata, 0);
      o_b <= total(rdata);
      o_c <= width(rdata);
      o_d <= moved(data, 0);
    end if;
  end process;
end architecture rtl;
