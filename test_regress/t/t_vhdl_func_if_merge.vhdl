-- DESCRIPTION: Verilator: VHDL if arms in a function body that run on data
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_func_if_merge is
  port (v   : in  std_logic_vector(15 downto 0);
        cnt : out std_logic_vector(4 downto 0);
        mix : out std_logic_vector(7 downto 0);
        k   : out std_logic_vector(4 downto 0);
        same : out std_logic_vector(7 downto 0));
end t_vhdl_func_if_merge;

architecture behav of t_vhdl_func_if_merge is
  -- One if per iteration of a 16-step loop: the rest of the body must not be
  -- lowered once per arm
  function count (vec : std_logic_vector) return std_logic_vector is
    variable res : std_logic_vector (4 downto 0) := (others => '0');
  begin
    for i in vec'range loop
      if vec (i) = '1' then
        res := std_logic_vector (unsigned (res) + 1);
      end if;
    end loop;
    return res;
  end count;

  -- Arms that assign different variables, an elsif, a missing else, and a nested if
  function mixer (x : std_logic_vector (7 downto 0)) return std_logic_vector is
    variable a : unsigned (7 downto 0) := unsigned (x);
    variable b : unsigned (7 downto 0) := to_unsigned (5, 8);
  begin
    for i in 0 to 7 loop
      if x (i) = '1' then
        if i mod 2 = 0 then
          a := a + 1;
        else
          b := b + a;
        end if;
      elsif i = 3 then
        b := b - 1;
      elsif x (7 - i) = '1' then
        a := a xor b;
      end if;
    end loop;
    return std_logic_vector (a + b);
  end mixer;

  -- Both arms give the variable the same new value
  function same_arms (x : std_logic_vector (15 downto 0)) return std_logic_vector is
    variable n : integer := 0;
  begin
    if x (0) = '1' then
      n := 4;
    else
      n := 4;
    end if;
    return std_logic_vector (to_unsigned (n, 8));
  end same_arms;

  constant kc : std_logic_vector (4 downto 0) := count (x"a5f3");
begin
  cnt <= count (v);
  mix <= mixer (v (7 downto 0));
  k <= kc;
  same <= same_arms (v);
end behav;
