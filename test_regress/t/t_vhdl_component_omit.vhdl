-- DESCRIPTION: Verilator: VHDL a component that omits entity ports and generics with defaults
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity leaf is
  generic (K : natural := 3);
  port (a   : in  std_logic_vector(3 downto 0);
        en  : in  std_logic := '1';
        y   : out std_logic_vector(3 downto 0);
        dbg : out std_logic);
end leaf;

architecture rtl of leaf is
begin
  y <= std_logic_vector (unsigned (a) + K) when en = '1' else (others => '0');
  dbg <= '1';
end rtl;

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_component_omit is
  port (i : in  std_logic_vector(3 downto 0);
        o : out std_logic_vector(3 downto 0));
end t_vhdl_component_omit;

architecture behav of t_vhdl_component_omit is
  -- The default binding leaves the generic K, the input en and the output dbg
  -- unassociated: K and en take their defaults
  component leaf
    port (a : in  std_logic_vector(3 downto 0);
          y : out std_logic_vector(3 downto 0));
  end component;
begin
  u : leaf port map (a => i, y => o);
end behav;
