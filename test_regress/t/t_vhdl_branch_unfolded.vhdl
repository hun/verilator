-- DESCRIPTION: Verilator: a branch guarded by constants that do not fold is not length-checked
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

-- The width of ram is one more than the data for "DROP", so the first branch, which
-- assigns the data as is, never runs there. Its length mismatch is a run-time matter
entity leaf is
  generic (
    Feat_g : string
  );
  port (
    d : in  std_logic_vector(7 downto 0);
    q : out std_logic_vector(8 downto 0)
  );
end entity leaf;

architecture rtl of leaf is
  -- A string argument is not folded
  function same(a : string; b : string) return boolean is
  begin
    return a = b;
  end function same;
  function choose(c : boolean; x : positive; y : positive) return positive is
  begin
    if c then
      return x;
    else
      return y;
    end if;
  end function choose;
  constant RamWidth_c : natural := choose(not same(Feat_g, "DROP"), 9, 10);
  signal ram : std_logic_vector(RamWidth_c - 2 downto 0);
begin
  process (all)
  begin
    if not same(Feat_g, "DROP") then
      ram <= d;
    elsif same(Feat_g, "DROP") then
      ram <= d & '0';
    else
      ram <= d;
    end if;
  end process;
  q <= '0' & ram when RamWidth_c = 9 else ram;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    d : in  std_logic_vector(7 downto 0);
    q : out std_logic_vector(8 downto 0)
  );
end entity t;

architecture rtl of t is
begin
  u : entity work.leaf
    generic map (Feat_g => "DROP")
    port map (d => d, q => q);
end architecture rtl;
