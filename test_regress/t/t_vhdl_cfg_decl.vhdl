-- DESCRIPTION: Verilator: VHDL configuration declaration selects an architecture
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The last analyzed architecture, inv, is the default; a configuration declaration
-- selects ident for some instances

entity child is
  port (
    i : in  bit;
    o : out bit
  );
end entity;

architecture ident of child is
begin
  o <= i;
end architecture;

architecture inv of child is
begin
  o <= not i;
end architecture;

entity t is
  port (
    i  : in  bit;
    o1 : out bit;
    o2 : out bit;
    o3 : out bit;
    o4 : out bit;
    o5 : out bit
  );
end entity;

architecture rtl of t is
  component child is
    port (
      i : in  bit;
      o : out bit
    );
  end component;

  -- The configuration declaration below overrides this
  for all : child use entity work.child(inv);
begin
  u1 : child port map (i => i, o => o1);
  u2 : child port map (i => i, o => o2);
  u3 : child port map (i => i, o => o3);
  u4 : child port map (i => i, o => o4);
  u5 : child port map (i => i, o => o5);
end architecture;

configuration cfg of t is
  use work.all;
  for rtl
    for u1 : child
      use entity work.child(ident);
    end for;
    for u3, u4 : child
      use entity work.child(ident);
    end for;
    for others : child
      use entity work.child(inv);
    end for;
  end for;
end cfg;
