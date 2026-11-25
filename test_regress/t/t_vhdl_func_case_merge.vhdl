-- DESCRIPTION: Verilator: VHDL case arms in a function body that run on data
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_func_case_merge is
  port (a  : in  std_logic_vector(7 downto 0);
        yc : out std_logic_vector(7 downto 0);
        yp : out std_logic_vector(7 downto 0));
end t_vhdl_func_case_merge;

architecture behav of t_vhdl_func_case_merge is
  -- A case per step, none of which leaves the function: the rest of the body must not be
  -- lowered once per arm
  function mix (x : std_logic_vector (7 downto 0)) return std_logic_vector is
    variable a : unsigned (7 downto 0) := unsigned (x);
    variable b : unsigned (7 downto 0) := to_unsigned (5, 8);
  begin
    case x (1 downto 0) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (0) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (2) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (2 downto 1) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (1) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (3) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (3 downto 2) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (2) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (4) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (4 downto 3) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (3) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (5) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (5 downto 4) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (4) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (6) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (6 downto 5) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (5) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (7) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (7 downto 6) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (6) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (0) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (1 downto 0) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (0) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (2) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (2 downto 1) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (1) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (3) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (3 downto 2) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (2) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (4) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (4 downto 3) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (3) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (5) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (5 downto 4) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (4) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (6) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (6 downto 5) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (5) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (7) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (7 downto 6) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (6) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (0) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (1 downto 0) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (0) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (2) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (2 downto 1) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (1) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (3) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (3 downto 2) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (2) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (4) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (4 downto 3) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (3) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (5) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (5 downto 4) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (4) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (6) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (6 downto 5) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (5) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (7) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (7 downto 6) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (6) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (0) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (1 downto 0) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (0) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (2) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (2 downto 1) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (1) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (3) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    case x (3 downto 2) is
      when "00" => a := a + 1;
      when "01" | "10" =>
        b := b xor a;
        if x (2) = '1' then a := a (6 downto 0) & '0'; end if;
      when "11" => a := a xor b; b := b + 3;
      when others => b := b - 1;
    end case;
    case x (4) = '1' is
      when true => b := b + 1;
      when false => null;
    end case;
    return std_logic_vector (a xor b);
  end mix;
begin
  yc <= mix (a);
  process (a)
    variable r : std_logic_vector (7 downto 0);
  begin
    r := mix (a);
    yp <= r;
  end process;
end behav;
