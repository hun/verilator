-- DESCRIPTION: Verilator: VHDL configuration declarations that select different architectures are refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Two configurations of one architecture cannot both apply, so a disagreement is refused

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

begin
  u1 : child port map (i => i, o => o1);
  u2 : child port map (i => i, o => o2);
  u3 : child port map (i => i, o => o3);
  u4 : child port map (i => i, o => o4);
  u5 : child port map (i => i, o => o5);
end architecture;

configuration cfg_a of t is
  for rtl
    for u1 : child
      use entity work.child(ident);
    end for;
  end for;
end configuration;

configuration cfg_b of t is
  for rtl
    for u1 : child
      use entity work.child(inv);
    end for;
  end for;
end configuration;
