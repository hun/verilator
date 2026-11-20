-- DESCRIPTION: Verilator: a string literal of logic characters in a string context
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A literal like "ZU" is a std_logic_vector value in a logic context ('Z' and 'U' are
-- std_logic literals) and a two-character string in a string context. poc's configuration
-- dispatches on a `string(3 to 4)` with such a choice, and reading it as a two-bit vector
-- left the case arm unmatchable.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    clk : in  std_logic;
    o   : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
  constant pair_c : string(3 to 4) := "ZU";

  function dispatch(s : string(3 to 4)) return integer is
  begin
    case s is
      when "7A" => return 1;
      when "ZU" => return 2;
      when others => return 3;
    end case;
  end function;

  constant chosen_c : integer := dispatch(pair_c);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      o <= std_logic_vector(to_unsigned(chosen_c, 8));
    end if;
  end process;
end architecture;
