-- DESCRIPTION: Verilator: a transport signal assignment at architecture level is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Transport delay is not modelled.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal a : bit;
begin
  o <= transport '1';
end architecture rtl;
