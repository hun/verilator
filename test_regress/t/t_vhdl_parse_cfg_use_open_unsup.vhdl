-- DESCRIPTION: Verilator: a use open binding is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Only entity and configuration bindings are handled.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
end architecture rtl;

configuration c of t is
  for rtl
    for u1 : comp use open;
    end for;
  end for;
end configuration c;
