-- DESCRIPTION: Verilator: an instance of a configuration
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity child is
  port (i : in std_logic;
        o : out std_logic);
end entity child;

architecture buf of child is
begin
  o <= i;
end architecture buf;

architecture inv of child is
begin
  o <= not i;
end architecture inv;

architecture other of child is
begin
  o <= '1';
end architecture other;

-- Each configuration selects an architecture, neither of them the one analyzed last
configuration child_buf of child is
  for buf
  end for;
end configuration child_buf;

configuration child_inv of child is
  for inv
  end for;
end configuration child_inv;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (i : in std_logic;
        kept : out std_logic;
        inverted : out std_logic;
        last : out std_logic);
end entity t;

architecture rtl of t is
begin
  u_buf : configuration work.child_buf port map (i => i, o => kept);
  u_inv : configuration work.child_inv port map (i => i, o => inverted);
  u_last : entity work.child port map (i => i, o => last);
end architecture rtl;
