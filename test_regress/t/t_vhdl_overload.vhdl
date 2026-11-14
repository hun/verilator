-- DESCRIPTION: Verilator: VHDL call bound to the overload whose formal takes the actual
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package ov_pkg is
  type int_vec_t is array (natural range <>) of integer;
  -- The array declaration comes first, so an integer actual must not bind its body
  function pick(vec : int_vec_t) return integer;
  function pick(value : integer) return integer;
  function flag(value : natural) return integer;
  function flag(value : boolean) return integer;
end package ov_pkg;

package body ov_pkg is
  function pick(vec : int_vec_t) return integer is
  begin
    return vec(1) + vec(0);
  end function pick;

  function pick(value : integer) return integer is
  begin
    return value + 100;
  end function pick;

  function flag(value : natural) return integer is
  begin
    return value + 10;
  end function flag;

  function flag(value : boolean) return integer is
  begin
    if value then
      return 5;
    else
      return 7;
    end if;
  end function flag;
end package body ov_pkg;

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.ov_pkg.all;

entity t is
  port (
    o_vec   : out integer;
    o_const : out integer;
    o_var   : out integer;
    o_nat   : out integer;
    o_bool  : out integer;
    o_fold  : out integer;
    o_bits  : out std_logic_vector(2 downto 0);
    o_sum   : out integer
  );
end entity t;

architecture rtl of t is
  constant width_c : integer := 12;
  -- A constraint needs the same choice and can only be answered by folding
  subtype row_t is std_logic_vector(pick(width_c) - 100 downto 1);
  -- The same choice inside a constant, where the call is folded rather than lowered
  constant folded_c : integer := pick(3) + pick(width_c);
  signal vec : int_vec_t(0 to 1);
  signal one : std_logic;
  signal count : integer;
begin
  vec <= (0 => 12, 1 => 40);
  one <= '1';
  count <= 7;
  o_vec <= pick(vec);
  o_const <= pick(width_c);
  o_var <= pick(count);
  o_nat <= flag(4);
  o_bool <= flag(one = '1');
  o_fold <= folded_c;

  -- The choice also has to be made where only folding answers: a generate range and a
  -- subtype constraint are static, so neither can fall back to a lowered call
  gen : for idx in 1 to pick(3) - 100 generate
  begin
    o_bits(idx - 1) <= '1';
  end generate gen;

  o_sum <= row_t'length;
end architecture rtl;
