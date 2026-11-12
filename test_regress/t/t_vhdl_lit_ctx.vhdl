-- DESCRIPTION: Verilator: VHDL character/string literal context
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    ch : in  std_logic_vector(7 downto 0);  -- a character code
    o  : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t is
  signal sa : string(1 to 2);
  signal sb : string(1 to 2);
  signal c  : character;
begin
  sa <= "ab";
  sb <= "ac";
  c  <= character'val(to_integer(unsigned(ch)));

  compute : process(all)
  begin
    if sa < sb then o(0) <= '1'; else o(0) <= '0'; end if;  -- string compare
    if c >= 'a' then o(1) <= '1'; else o(1) <= '0'; end if;  -- char literal compare
    if c <= 'z' then o(2) <= '1'; else o(2) <= '0'; end if;
    if sa = "ab" then o(3) <= '1'; else o(3) <= '0'; end if;  -- equal strings
  end process;
end architecture;
