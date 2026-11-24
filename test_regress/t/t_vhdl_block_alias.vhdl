-- DESCRIPTION: Verilator: an alias in the declarative part of a block
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity sub is
  port (a : in std_logic_vector(7 downto 0); o : out std_logic_vector(7 downto 0));
end;

architecture r of sub is
  signal acc : std_logic_vector(7 downto 0);
begin
  acc <= not a;
  o <= a;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_block_alias is
  port (a : in std_logic_vector(7 downto 0);
        hi : out std_logic_vector(3 downto 0);
        ext : out std_logic_vector(7 downto 0));
end;

architecture r of t_vhdl_block_alias is
  signal o : std_logic_vector(7 downto 0);
begin
  dut : entity work.sub port map (a => a, o => o);

  b1 : block
    signal s : std_logic_vector(7 downto 0);
    alias top_nibble : std_logic_vector(3 downto 0) is s(7 downto 4);
    alias accum : std_logic_vector(7 downto 0) is << signal dut.acc : std_logic_vector(7 downto 0) >>;
  begin
    s <= a;
    hi <= top_nibble;
    ext <= accum;
  end block;
end;
