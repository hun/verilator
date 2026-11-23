-- DESCRIPTION: Verilator: PSL properties, sequences and assume directives
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    clk : in  bit;
    a   : in  bit;
    o   : out bit
  );
end entity t;

architecture rtl of t is
  default clock is clk'event and clk = '1';
begin
  o <= a;

  sequence s0 is {a ; not a};
  sequence s1 is {s0 ; s0};
  property p0 is always a -> next a;
  assume always a = '1';
  psl_b : assume a = '1';
  assert p0;
  assert (always s1);
end architecture rtl;
