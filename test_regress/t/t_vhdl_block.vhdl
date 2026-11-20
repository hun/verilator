-- DESCRIPTION: Verilator: VHDL block, generate and process declarative parts
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i_a   : in  std_logic_vector(3 downto 0);
    o_blk : out std_logic_vector(3 downto 0);
    o_gen : out std_logic_vector(3 downto 0);
    o_pro : out std_logic_vector(3 downto 0)
    );
end entity;

architecture rtl of t is
  attribute keep : string;
begin
  -- Each declarative part names its own constant in a subtype declared alongside it
  blk : block
    constant cw : positive := 4;
    subtype word_t is std_logic_vector(cw - 1 downto 0);
    signal r : word_t := (others => '0');
    attribute keep of r : signal is "true";
  begin
    r <= i_a;
    o_blk <= r;
  end block;

  gny : for gen_i in 0 to 0 generate
    constant gw : positive := 4;
    subtype gword_t is std_logic_vector(gw - 1 downto 0);
    signal s : gword_t := (others => '1');
  begin
    s <= i_a;
    o_gen <= s;
  end generate;

  prc : process (i_a)
    constant pw : positive := 4;
    subtype pword_t is std_logic_vector(pw - 1 downto 0);
    variable v : pword_t;
  begin
    v := i_a;
    o_pro <= v;
  end process;
end architecture;
