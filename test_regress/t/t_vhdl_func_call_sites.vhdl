-- DESCRIPTION: Verilator: VHDL function calls that need a temporary in every statement position
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_func_call_sites is
  port (a, b : in  std_logic_vector(7 downto 0);
        y1, y2, y3, y4, y5, y6 : out std_logic_vector(7 downto 0);
        yc1, yc2 : out std_logic_vector(7 downto 0));
end t_vhdl_func_call_sites;

architecture behav of t_vhdl_func_call_sites is
  -- Large enough that its result is held in a temporary at the call
  function hash (x : std_logic_vector (7 downto 0)) return std_logic_vector is
    variable v : std_logic_vector (7 downto 0) := x;
  begin
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    return v;
  end hash;
begin
  process (a, b)
    variable x   : std_logic_vector (7 downto 0);
    variable acc : std_logic_vector (7 downto 0);
    variable n   : integer;
  begin
    -- A condition that is read again on every iteration
    x := a;
    n := 0;
    while hash (x) /= x"3c" and n < 40 loop
      x := std_logic_vector (unsigned (x) + 1);
      n := n + 1;
    end loop;
    y1 <= x;

    x := b;
    n := 0;
    loop
      exit when hash (x) (0) = '1' or n = 20;
      x := std_logic_vector (unsigned (x) + 3);
      n := n + 1;
    end loop;
    y2 <= x;

    acc := (others => '0');
    for i in 0 to 7 loop
      next when hash (std_logic_vector (unsigned (b) + i)) (1) = '0';
      acc := std_logic_vector (unsigned (acc) + 1);
    end loop;
    y3 <= acc;

    case hash (a) (1 downto 0) is
      when "00"   => y4 <= x"11";
      when "01"   => y4 <= x"22";
      when "10"   => y4 <= x"33";
      when others => y4 <= x"44";
    end case;

    if hash (a) = x"00" then
      y5 <= x"01";
    elsif hash (b) (7) = '1' then
      y5 <= x"02";
    else
      y5 <= hash (a xor b);
    end if;

    -- Past the unroll limit, so the loop runs at run time
    acc := a;
    for i in 0 to 5000 loop
      acc := hash (acc xor std_logic_vector (to_unsigned (i mod 256, 8)));
    end loop;
    y6 <= acc;

    assert hash (a) = hash (a) report "hash differs" severity failure;
  end process;

  yc1 <= a when hash (b) = x"00" else b;
  with hash (a) (1 downto 0) select
    yc2 <= x"aa" when "00",
           x"bb" when "01",
           x"cc" when others;
end behav;
